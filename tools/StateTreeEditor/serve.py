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
"""

import argparse
import json
import os
import sys
import tempfile
import threading
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

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


class Handler(BaseHTTPRequestHandler):
    server_version = "StateTreeEditor/1"
    root = ""
    eol = "auto"
    lock = threading.Lock()

    def log_message(self, fmt, *args):
        if self.server.verbose:
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def send_body(self, status, body, content_type, headers=None):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (headers or {}).items():
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
    parser.add_argument("--open", action="store_true", help="open the editor in the browser")
    parser.add_argument("--verbose", action="store_true", help="log every request")
    args = parser.parse_args()

    root = os.path.realpath(args.root)
    if not os.path.isdir(root):
        print("no such directory: " + root, file=sys.stderr)
        return 1
    Handler.root = root
    Handler.eol = args.eol
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
