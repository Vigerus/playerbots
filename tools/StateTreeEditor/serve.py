"""
Local server of the state tree editor: serves the editor (index.html, editor.js, editor.css, schema.json) and a JSON
API over the tree files of the statetree directory. Binds to 127.0.0.1 only.

    python tools/StateTreeEditor/serve.py [--port 8765] [--root ../../statetree] [--open] [--eol auto|crlf|lf]

API (paths are relative to the root, '/' separated, only *.xml):
    GET  /api/files                  {"root", "files": [{"path", "size", "mtime"}], "folders": [...]}
    GET  /api/file?path=<rel>        the file's text (header X-Mtime)
    PUT  /api/file?path=<rel>        saves the body (POST works too); header X-Base-Mtime: refused with 409 when the
                                     file changed on disk since then (add &force=1 to overwrite)
    POST /api/new?path=<rel>         creates the file from the body (or a minimal tree when empty); 409 if it exists
    GET  /api/schema                 schema.json (also /schema.json)
    GET  /api/spells?q=<id or name>  spell suggestions from the server's Spell.dbc (--dbc): [{"id", "name", "rank", "icon"}]
    GET  /api/spell?id=<id or name>  {"id", "name", "rank", "icon", "tooltip"}: the tooltip html from wowhead (classic)
    GET  /api/icon?spell=<id or name>  the spell's icon (jpg) from wowhead
Wowhead answers are cached in cache/ next to this script.
"""

import argparse
import json
import os
import re
import struct
import sys
import tempfile
import threading
import urllib.request
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
STATIC = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/index.html": ("index.html", "text/html; charset=utf-8"),
    "/editor.js": ("editor.js", "text/javascript; charset=utf-8"),
    "/editor.css": ("editor.css", "text/css; charset=utf-8"),
    "/schema.json": ("schema.json", "application/json; charset=utf-8"),
    "/api/schema": ("schema.json", "application/json; charset=utf-8"),
}
MAX_BODY = 16 * 1024 * 1024
CACHE = os.path.join(HERE, "cache")
WOWHEAD_TOOLTIP = "https://nether.wowhead.com/classic/tooltip/spell/%d"
WOWHEAD_ICON = "https://wow.zamimg.com/images/wow/icons/medium/%s.jpg"
ICON_NAME = re.compile(r"^[a-z0-9_\-]+$")

TEMPLATE = """<?xml version="1.0" encoding="utf-8"?>
<StateTree version="2">
  <Comment>new state tree</Comment>
  <State name="Idle">
    <Tasks>
      <Task class="Task_Wait">
        <Delay value="0"/>
      </Task>
    </Tasks>
  </State>
</StateTree>
"""


class PathError(ValueError):
    pass


def resolve(root, rel):
    """absolute path of a *.xml file under root, PathError for anything else"""
    if not rel or "\0" in rel:
        raise PathError("missing path")
    rel = rel.replace("\\", "/")
    if rel.startswith("/") or (len(rel) > 1 and rel[1] == ":"):
        raise PathError("absolute paths are not allowed")
    parts = rel.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise PathError("'.', '..' and empty path segments are not allowed")
    if not rel.lower().endswith(".xml"):
        raise PathError("only *.xml files")
    path = os.path.realpath(os.path.join(root, *parts))
    try:
        inside = os.path.commonpath([path, root]) == root and path != root
    except ValueError:
        inside = False
    if not inside:
        raise PathError("outside the root")
    return path


def mtime_of(path):
    return int(os.stat(path).st_mtime_ns // 1000000)


def write_atomic(path, text, eol):
    data = text.replace("\r\n", "\n")
    if eol == "crlf":
        data = data.replace("\n", "\r\n")
    directory = os.path.dirname(path)
    handle, temp = tempfile.mkstemp(prefix="." + os.path.basename(path) + ".", suffix=".tmp", dir=directory)
    try:
        with os.fdopen(handle, "wb") as file:
            file.write(data.encode("utf-8"))
            file.flush()
            os.fsync(file.fileno())
        os.replace(temp, path)
    except BaseException:
        if os.path.exists(temp):
            os.remove(temp)
        raise


def eol_of(path, policy):
    """line ending to save with: the policy, or for auto the existing file's (CRLF for new files)"""
    if policy != "auto":
        return policy
    if not os.path.exists(path):
        return "crlf"
    with open(path, "rb") as file:
        return "crlf" if b"\r\n" in file.read() else "lf"


def read_dbc(path):
    """records of a WDBC file as uint32 tuples, and a function reading a string by offset"""
    with open(path, "rb") as file:
        data = file.read()
    magic, count, fields, size, _ = struct.unpack_from("<4s4I", data)
    if magic != b"WDBC":
        raise ValueError(path + " is not a DBC file")
    strings = data[20 + count * size:]
    rows = [struct.unpack_from("<%dI" % fields, data, 20 + i * size) for i in range(count)]
    return rows, lambda offset: strings[offset:strings.index(b"\0", offset)].decode("utf-8", "replace")


class SpellIndex:
    """id, name, rank and icon of every spell in the 1.12 Spell.dbc (field numbers as in DBCStructure.h SpellEntry)"""
    ID, ATTRIBUTES, ICON, NAME, RANK, FAMILY = 0, 6, 117, 120, 129, 160
    ATTR_PASSIVE = 0x40

    def __init__(self, dbc_dir):
        icons = {}
        rows, text = read_dbc(os.path.join(dbc_dir, "SpellIcon.dbc"))
        for row in rows:
            icons[row[0]] = text(row[1]).replace("\\", "/").rsplit("/", 1)[-1].lower()
        self.spells = {}
        rows, text = read_dbc(os.path.join(dbc_dir, "Spell.dbc"))
        for row in rows:
            name = text(row[self.NAME])
            if name:
                self.spells[row[self.ID]] = {"id": row[self.ID], "name": name, "rank": text(row[self.RANK]),
                                             "icon": icons.get(row[self.ICON], ""),
                                             "family": row[self.FAMILY],
                                             "passive": bool(row[self.ATTRIBUTES] & self.ATTR_PASSIVE)}

    @staticmethod
    def preference(spell):
        # class spells first (the trees use those), then the ones with an icon and not passive
        return (spell["family"] == 0, not spell["icon"], spell["passive"])

    def search(self, query, limit):
        query = query.strip().casefold()
        if not query:
            return []
        found = []
        for spell in self.spells.values():
            if query.isdigit():
                id_text = str(spell["id"])
                if not id_text.startswith(query):
                    continue
                match = (id_text != query, len(id_text))
            else:
                name = spell["name"].casefold()
                if name == query:
                    match = (0,)
                elif name.startswith(query):
                    match = (1,)
                elif (" " + name).find(" " + query) >= 0:
                    match = (2,)
                elif query in name:
                    match = (3,)
                else:
                    continue
            found.append((match, self.preference(spell), spell["name"], spell["id"], spell))
        found.sort(key=lambda entry: entry[:4])
        return [self.public(entry[-1]) for entry in found[:limit]]

    def find(self, key):
        """the spell of an id, or the preferred one of a name (spellid="Shield Bash" means any rank)"""
        key = str(key).strip()
        if key.isdigit():
            return self.spells.get(int(key))
        name = key.casefold()
        same = [s for s in self.spells.values() if s["name"].casefold() == name]
        return min(same, key=lambda s: (self.preference(s), s["id"])) if same else None

    @staticmethod
    def public(spell):
        return {"id": spell["id"], "name": spell["name"], "rank": spell["rank"], "icon": spell["icon"]}


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0 StateTreeEditor"})
    with urllib.request.urlopen(request, timeout=10) as response:
        return response.read()


def cached(name, url):
    """the bytes of url, kept in CACHE/name; None when it cannot be fetched"""
    path = os.path.join(CACHE, *name.split("/"))
    if os.path.exists(path):
        with open(path, "rb") as file:
            return file.read()
    try:
        data = fetch(url)
    except OSError:
        return None
    os.makedirs(os.path.dirname(path), exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=os.path.dirname(path))
    with os.fdopen(handle, "wb") as file:
        file.write(data)
    try:
        os.replace(temp, path)
    except OSError:
        # another request for the same file wrote it first and is reading it (Windows refuses the replace)
        os.remove(temp)
    return data


class Handler(BaseHTTPRequestHandler):
    server_version = "StateTreeEditor/1"
    root = ""
    eol = "auto"
    spells = None
    lock = threading.Lock()

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def send_body(self, status, body, content_type, headers=None):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        headers = headers or {}
        if "Cache-Control" not in headers:
            self.send_header("Cache-Control", "no-store")
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def send_json(self, status, value, headers=None):
        self.send_body(status, json.dumps(value), "application/json; charset=utf-8", headers)

    def fail(self, status, message):
        self.send_json(status, {"ok": False, "error": message})

    def read_body(self):
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_BODY:
            raise PathError("body too large")
        return self.rfile.read(length).decode("utf-8") if length else ""

    def query(self):
        url = urlparse(self.path)
        return url.path, {key: values[-1] for key, values in parse_qs(url.query).items()}

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path, query = self.query()
        try:
            if path in STATIC:
                name, content_type = STATIC[path]
                file = os.path.join(HERE, name)
                if not os.path.exists(file):
                    return self.fail(HTTPStatus.NOT_FOUND, name + " not found (run gen_schema.py for schema.json)")
                with open(file, "rb") as handle:
                    return self.send_body(HTTPStatus.OK, handle.read(), content_type)
            if path == "/api/files":
                return self.send_json(HTTPStatus.OK, self.list_files())
            if path == "/api/file":
                file = resolve(self.root, query.get("path", ""))
                if not os.path.isfile(file):
                    return self.fail(HTTPStatus.NOT_FOUND, "no such file")
                with open(file, "rb") as handle:
                    text = handle.read().decode("utf-8")
                return self.send_body(HTTPStatus.OK, text, "text/plain; charset=utf-8", {"X-Mtime": str(mtime_of(file))})
            if path == "/api/spells":
                if not self.spells:
                    return self.send_json(HTTPStatus.OK, {"spells": [], "error": "no Spell.dbc (start with --dbc)"})
                limit = min(int(query["limit"]), 200) if query.get("limit", "").isdigit() else 40
                return self.send_json(HTTPStatus.OK, {"spells": self.spells.search(query.get("q", ""), limit)})
            if path == "/api/spell":
                spell = self.spell_info(query.get("id", ""))
                if not spell:
                    return self.fail(HTTPStatus.NOT_FOUND, "unknown spell")
                return self.send_json(HTTPStatus.OK, spell)
            if path == "/api/icon":
                spell = self.spell_info(query.get("spell", ""), tooltip=False)
                icon = spell and spell["icon"]
                data = cached("icons/" + icon + ".jpg", WOWHEAD_ICON % quote(icon)) if icon and ICON_NAME.match(icon) else None
                if not data:
                    return self.fail(HTTPStatus.NOT_FOUND, "no icon")
                return self.send_body(HTTPStatus.OK, data, "image/jpeg", {"Cache-Control": "max-age=86400"})
            return self.fail(HTTPStatus.NOT_FOUND, "unknown path")
        except PathError as error:
            return self.fail(HTTPStatus.BAD_REQUEST, str(error))
        except UnicodeDecodeError:
            return self.fail(HTTPStatus.UNPROCESSABLE_ENTITY, "the file is not UTF-8")

    def do_PUT(self):
        path, query = self.query()
        if path != "/api/file":
            return self.fail(HTTPStatus.NOT_FOUND, "unknown path")
        self.save(query, create=False)

    def do_POST(self):
        path, query = self.query()
        if path == "/api/file":
            return self.save(query, create=False)
        if path == "/api/new":
            return self.save(query, create=True)
        return self.fail(HTTPStatus.NOT_FOUND, "unknown path")

    def save(self, query, create):
        try:
            file = resolve(self.root, query.get("path", ""))
            text = self.read_body()
        except PathError as error:
            return self.fail(HTTPStatus.BAD_REQUEST, str(error))
        except UnicodeDecodeError:
            return self.fail(HTTPStatus.BAD_REQUEST, "the body is not UTF-8")
        with self.lock:
            exists = os.path.exists(file)
            if create:
                if exists:
                    return self.fail(HTTPStatus.CONFLICT, "the file exists already")
                text = text or TEMPLATE
                os.makedirs(os.path.dirname(file), exist_ok=True)
            else:
                if not exists:
                    return self.fail(HTTPStatus.NOT_FOUND, "no such file (use /api/new to create one)")
                base = self.headers.get("X-Base-Mtime")
                if base and query.get("force") != "1" and str(mtime_of(file)) != base:
                    return self.fail(HTTPStatus.CONFLICT, "the file changed on disk since it was loaded")
            if not text.strip():
                return self.fail(HTTPStatus.BAD_REQUEST, "empty text")
            eol = eol_of(file, self.eol)
            try:
                write_atomic(file, text, eol)
            except OSError as error:
                return self.fail(HTTPStatus.INTERNAL_SERVER_ERROR, str(error))
            rel = os.path.relpath(file, self.root).replace("\\", "/")
            self.send_json(HTTPStatus.CREATED if create else HTTPStatus.OK,
                           {"ok": True, "path": rel, "mtime": mtime_of(file), "eol": eol,
                            "bytes": os.path.getsize(file)})

    def spell_info(self, key, tooltip=True):
        """the spell of an id or name: Spell.dbc first, wowhead's tooltip for the rest (or everything without --dbc)"""
        spell = self.spells.find(key) if self.spells else None
        spell = dict(SpellIndex.public(spell)) if spell else None
        spell_id = spell["id"] if spell else int(key) if str(key).isdigit() else None
        if spell_id is None or (spell and spell["icon"] and not tooltip):
            return spell
        data = cached("tooltips/%d.json" % spell_id, WOWHEAD_TOOLTIP % spell_id)
        try:
            wowhead = json.loads(data) if data else {}
        except ValueError:
            wowhead = {}
        if not spell and not wowhead.get("name"):
            return None
        spell = spell or {"id": spell_id, "name": wowhead["name"], "rank": "", "icon": ""}
        spell["icon"] = spell["icon"] or wowhead.get("icon", "")
        spell["tooltip"] = wowhead.get("tooltip", "")
        return spell

    def list_files(self):
        files = []
        folders = set()
        for directory, dirs, names in os.walk(self.root):
            dirs[:] = sorted(d for d in dirs if not d.startswith("."))
            rel_dir = os.path.relpath(directory, self.root).replace("\\", "/")
            if rel_dir != ".":
                folders.add(rel_dir)
            for name in sorted(names):
                if not name.lower().endswith(".xml") or name.startswith("."):
                    continue
                path = os.path.join(directory, name)
                stat = os.stat(path)
                rel = name if rel_dir == "." else rel_dir + "/" + name
                files.append({"path": rel, "size": stat.st_size, "mtime": int(stat.st_mtime_ns // 1000000)})
        return {"root": self.root, "files": files, "folders": sorted(folders)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--root", default=os.path.join(HERE, "..", "..", "statetree"),
                        help="directory of the tree files (default: ../../statetree of this script)")
    parser.add_argument("--eol", choices=("auto", "crlf", "lf"), default="auto",
                        help="line endings of saved files; auto keeps the file's own (CRLF for new files)")
    parser.add_argument("--dbc", help="the server's dbc directory (Spell.dbc, SpellIcon.dbc): spell lookup by name")
    parser.add_argument("--open", action="store_true", help="open the editor in the browser")
    parser.add_argument("--verbose", action="store_true", help="log every request")
    args = parser.parse_args()

    root = os.path.realpath(args.root)
    if not os.path.isdir(root):
        print("no such directory: " + root, file=sys.stderr)
        return 1
    Handler.root = root
    Handler.eol = args.eol
    if args.dbc:
        try:
            Handler.spells = SpellIndex(args.dbc)
            print("Spell lookup: %d spells from %s" % (len(Handler.spells.spells), args.dbc), flush=True)
        except (OSError, ValueError) as error:
            print("no spell lookup: %s" % error, file=sys.stderr)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.verbose = args.verbose
    url = "http://127.0.0.1:%d/" % server.server_address[1]
    print("State tree editor: %s  (root %s)" % (url, root), flush=True)
    if args.open:
        threading.Timer(0.3, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
