"""
Builds tools/StateTreeEditor/schema.json for the state tree editor from the sources and the trees:

- task classes: the RegisterTask("<XmlClass>", { &CppClass::Validate, ... }) calls of playerbot/StateTree, their
  properties and delegates (GetProperties / GetDelegates, inherited from the base class when not overridden) with the
  C++ types of the context members, whether they are global tasks (data providers)
- task settings: the child elements (and their attributes) and the <Task> attributes seen in the trees and in the
  task table of statetree/README.md, the "does" / "settings" columns of that table as description / settingsHelp
- condition and consideration classes: the "Node classes" table of statetree/STATETREE_FORMAT.md (attributes below),
  plus any RegisterCondition / RegisterConsideration of the sources

Usage:
    python gen_schema.py [--repo <playerbots dir>] [--out <schema.json>]
"""

import argparse
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO = os.path.normpath(os.path.join(HERE, "..", ".."))

TASK_STANDARD_ATTRIBUTES = {"class", "name", "completion", "show_log", "enabled"}
TASK_STRUCTURE_CHILDREN = {"Comment", "Binding", "Conditions"}
# tree structure (v1 and v2) in the examples of the class comments, no task settings
STRUCTURE_TAGS = {"Root", "Global", "GlobalTasks", "Node", "State", "StateTree", "Tasks", "Task", "Transitions",
                  "Transition", "Conditions", "EnterConditions", "Condition", "Any", "All", "Binding", "Comment",
                  "Considerations", "Consideration"}
GLOBAL_BASES = {"STAI_GlobalTask", "STAI_DataProviderBase"}
MAX_EXAMPLES = 8

ARITHMETIC = {
    "bool", "char", "short", "int", "long", "unsigned", "float", "double", "size_t",
    "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "unsigned int", "unsigned long", "long long", "unsigned long long", "unsigned char", "signed char",
}
CONTAINERS = ("std::vector<", "std::list<", "std::deque<", "std::set<", "std::map<", "std::unordered_map<",
              "std::unordered_set<", "std::multimap<", "std::array<")
MAPS = ("std::map<", "std::unordered_map<", "std::multimap<")

# condition / consideration attributes of the "Node classes" table of STATETREE_FORMAT.md (descriptions read from it)
CONDITIONS = {
    "Condition_Compare": [
        {"name": "binding", "kind": "property", "required": True,
         "help": "path of a task property: Root|<TaskClass>::<property> or Root|<state>|<TaskClass>::<property>"},
        {"name": "compare", "kind": "enum", "options": ["equal", "lesser", "lesser or equal", "greater", "greater or equal"],
         "help": "without: passes while the property is set (a container not empty, else its bool value)"},
        {"name": "value", "kind": "text", "help": "the number (or a text with compare=\"equal\") to compare with"},
        {"name": "key", "kind": "number", "help": "the entry of a map of numbers instead of the property itself"},
    ],
    "Condition_RandomChance": [
        {"name": "chance", "kind": "number", "min": 0, "max": 100, "help": "percent, 0..100"},
    ],
    "Condition_ActiveStateLocation": [
        {"name": "location", "kind": "flags", "options": ["above", "this", "below"], "default": "above,below",
         "help": "comma separated; default above,below: \"don't reenter\""},
    ],
    "Condition_TimeInState": [
        {"name": "ms", "kind": "number", "required": True, "min": 0, "help": "ms the state has been active"},
    ],
}
CONSIDERATIONS = {
    "Consideration_Constant": [
        {"name": "value", "kind": "number", "min": 0, "max": 1, "help": "a constant score 0..1"},
    ],
    "Consideration_Number": [
        {"name": "binding", "kind": "property", "required": True, "number": True, "help": "path of a number property"},
        {"name": "min", "kind": "number", "default": "0", "help": "the input mapped to 0"},
        {"name": "max", "kind": "number", "default": "1", "help": "the input mapped to 1"},
    ],
}
CONSIDERATION_CHILDREN = {"Consideration_Number": {"Key": ["x", "y"]}}


def read(path):
    with open(path, encoding="utf-8", errors="replace") as file:
        return file.read()


def strip_comments(code):
    """C++ comments replaced by spaces (newlines kept), string literals untouched"""
    def repl(match):
        text = match.group(0)
        if text.startswith("/"):
            return re.sub(r"[^\n]", " ", text)
        return text
    return re.sub(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'|//[^\n]*|/\*.*?\*/', repl, code, flags=re.S)


def match_brace(code, open_index):
    """index of the } closing the { at open_index"""
    depth = 0
    for index in range(open_index, len(code)):
        char = code[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(code) - 1


def flatten_braces(body):
    """the body with the content of nested {...} blocks removed: only the declarations at depth 0 remain"""
    out = []
    depth = 0
    for char in body:
        if char == "{":
            if depth == 0:
                out.append("{")
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                out.append("}")
        elif depth == 0:
            out.append(char)
    return "".join(out)


def norm_type(text):
    text = re.sub(r"\s+", " ", text.strip())
    text = re.sub(r"\s*([<>,*&])\s*", r"\1", text)
    text = re.sub(r"^(?:const |mutable |static |inline )+", "", text)
    return text


class Sources:
    def __init__(self, root):
        self.root = root
        self.files = {}         # relative path -> code without comments
        self.raw = {}           # relative path -> code with comments
        for directory, _, names in os.walk(root):
            for name in sorted(names):
                if name.endswith((".h", ".cpp")):
                    path = os.path.join(directory, name)
                    rel = os.path.relpath(path, os.path.dirname(os.path.dirname(root))).replace("\\", "/")
                    self.raw[rel] = read(path)
                    self.files[rel] = strip_comments(self.raw[rel])
        self.classes = {}       # name -> {bases, body, file, start}
        self.aliases = {}
        self.registrations = []     # (kind, xml class, cpp class)
        self._parse()

    def _parse(self):
        class_re = re.compile(r"\b(class|struct)\s+(\w+)\s*(?:final\s*)?(?::\s*([^{;()]+))?\{")
        for rel, code in self.files.items():
            for match in class_re.finditer(code):
                name = match.group(2)
                bases = []
                if match.group(3):
                    for base in match.group(3).split(","):
                        base = re.sub(r"\b(public|protected|private|virtual)\b", "", base).strip()
                        if base:
                            bases.append(base.split("::")[-1])
                open_index = match.end() - 1
                close = match_brace(code, open_index)
                if name not in self.classes or not self.classes[name]["body"]:
                    self.classes[name] = {"bases": bases, "body": code[open_index + 1:close], "file": rel,
                                          "start": match.start()}
            for match in re.finditer(r"\busing\s+(\w+)\s*=\s*([^;]+);", code):
                self.aliases.setdefault(match.group(1), norm_type(match.group(2)))
            for match in re.finditer(r"\bRegister(Task|Condition|Consideration)\s*\(\s*\"(\w+)\"\s*,\s*\{?\s*&?\s*([\w:]+?)::(?:Validate|Create)\b", code):
                self.registrations.append((match.group(1), match.group(2), match.group(3).split("::")[-1]))

    def ancestors(self, name):
        seen = []
        stack = list(self.classes.get(name, {}).get("bases", []))
        while stack:
            base = stack.pop(0)
            if base in seen:
                continue
            seen.append(base)
            stack.extend(self.classes.get(base, {}).get("bases", []))
        return seen

    def defs_function(self, cls, function):
        """the Of(...) entries of cls::GetProperties / GetDelegates, None when cls does not define it"""
        pattern = re.compile(r"\b%s::%s\s*\(\s*\)\s*const\s*\{" % (re.escape(cls), function))
        for code in self.files.values():
            match = pattern.search(code)
            if match:
                close = match_brace(code, match.end() - 1)
                return code[match.end():close]
        info = self.classes.get(cls)
        if info:
            match = re.search(r"\b%s\s*\(\s*\)\s*const\s*(?:override\s*)?(?:final\s*)?\{" % function, info["body"])
            if match:
                close = match_brace(info["body"], match.end() - 1)
                return info["body"][match.end():close]
        return None

    def member_type(self, struct, member):
        for name in [struct] + self.ancestors(struct):
            info = self.classes.get(name)
            if not info:
                continue
            body = flatten_braces(info["body"])
            body = re.sub(r"\b(public|protected|private)\s*:", ";", body)
            match = re.search(r"(?:^|[;{}])\s*((?:(?:const|mutable|static|inline)\s+)*[\w:]+(?:\s*<[^;{}()]*>)?(?:\s*[*&]+)?)\s+%s\s*(?:=[^;]*|\{\s*\})?\s*;"
                              % re.escape(member), body)
            if match:
                return norm_type(match.group(1))
        return None

    def class_comment_lines(self, name):
        """the // lines right above the class declaration"""
        info = self.classes.get(name)
        if not info:
            return []
        raw = self.raw[info["file"]]
        before = raw[:info["start"]].rstrip().split("\n")
        lines = []
        for line in reversed(before):
            line = line.strip()
            if not line.startswith("//"):
                break
            lines.insert(0, line[2:].strip())
        return lines

    def class_comment(self, name):
        text = []
        for line in self.class_comment_lines(name):
            if line.startswith("<") or not line:
                break
            text.append(line)
        return " ".join(text).strip()

    def defs(self, cls, function):
        """(entries, defined in) of GetProperties / GetDelegates, inherited from the first base that defines it"""
        for name in [cls] + self.ancestors(cls):
            body = self.defs_function(name, function)
            if body is not None:
                return re.findall(r"STAI_(?:Property|Delegate)Def::Of\s*\(\s*\"(\w+)\"\s*,\s*&\s*([\w:]+)::(\w+)\s*\)", body), name
        return [], None


def type_traits(type_name, aliases, empty_types):
    resolved = aliases.get(type_name, type_name)
    traits = []
    is_number = resolved in ARITHMETIC
    is_container = resolved.startswith(CONTAINERS) or resolved == "std::string"
    if is_number:
        traits.append("number")
    if resolved == "std::string":
        traits.append("text")
    if resolved.startswith(MAPS):
        inner = resolved[resolved.index("<") + 1:-1]
        parts = inner.split(",")
        if len(parts) >= 2 and parts[0] in ARITHMETIC and parts[1] in ARITHMETIC:
            traits.append("numberMap")
    if is_number or is_container or resolved in empty_types:
        traits.append("testable")
    if is_container:
        traits.append("container")
    return traits


def unescape_md(text):
    return text.replace("\\|", "|").strip()


def readme_table(path):
    """task class -> (settings, does) of the README task table"""
    rows = {}
    if not os.path.exists(path):
        return rows
    in_table = False
    for line in read(path).split("\n"):
        if re.match(r"^\|\s*class\s*\|\s*settings\s*\|\s*does\s*\|", line):
            in_table = True
            continue
        if in_table:
            if not line.startswith("|"):
                in_table = False
                continue
            if line.startswith("|--"):
                continue
            cells = re.split(r"(?<!\\)\|", line)
            if len(cells) < 4:
                continue
            settings = unescape_md(cells[2])
            does = unescape_md("|".join(cells[3:-1]) if len(cells) > 4 else cells[3])
            for cls in re.findall(r"`(\w+)`", cells[1]):
                rows[cls] = (settings, does)
    return rows


def spec_node_table(path):
    """condition / consideration class -> does, from the Node classes table of STATETREE_FORMAT.md"""
    rows = {}
    if not os.path.exists(path):
        return rows
    for line in read(path).split("\n"):
        match = re.match(r"^\|\s*`((?:Condition|Consideration)_\w+)`\s*\|(.*)\|(.*)\|\s*$", line)
        if match:
            rows[match.group(1)] = (unescape_md(match.group(2)), unescape_md(match.group(3)))
    return rows


def short_description(text, limit=240):
    text = re.sub(r"\s+", " ", text).strip()
    if len(text) <= limit:
        return text
    cut = text[:limit]
    for separator in ("; ", ". ", ": ", ", "):
        index = cut.rfind(separator)
        if index > limit // 2:
            return cut[:index].rstrip(" ;:,") + " ..."
    return cut[:cut.rfind(" ")] + " ..."


def add_example(store, value):
    if value not in store and len(store) < MAX_EXAMPLES:
        store.append(value)


def settings_from_readme(settings):
    """(elements {tag: {attr: {examples, options}}}, task attributes {attr: {...}}) named in a settings cell"""
    elements = {}
    for match in re.finditer(r"`<(\w+)((?:\s+\w+=\"[^\"]*\")*)\s*(?:\.\.\.\s*)?/?>`", settings):
        tag = match.group(1)
        if tag in TASK_STRUCTURE_CHILDREN or tag == "Task":
            continue
        attributes = elements.setdefault(tag, {})
        for name, value in re.findall(r"(\w+)=\"([^\"]*)\"", match.group(2)):
            entry = attributes.setdefault(name, {"examples": []})
            if "|" in value and not value.startswith("<"):
                entry.setdefault("options", [])
                for option in value.split("|"):
                    if option and option not in entry["options"] and not option.startswith("<"):
                        entry["options"].append(option)
            elif value and value != "..." and not value.startswith("<"):
                add_example(entry["examples"], value)
    task_attributes = {}
    if "on the `<Task>`" in settings:
        for name, value in re.findall(r"`(\w+)=\"([^\"]*)\"`", settings):
            if name in TASK_STANDARD_ATTRIBUTES:
                continue
            entry = task_attributes.setdefault(name, {"examples": []})
            if "|" in value:
                entry.setdefault("options", [o for o in value.split("|") if o and not o.startswith("<")])
            elif value and value != "...":
                add_example(entry["examples"], value)
    return elements, task_attributes


def settings_from_comment(lines, xml_class):
    """(elements, task attributes) of the XML examples in a class's doc comment"""
    elements = {}
    task_attributes = {}
    for line in lines:
        for match in re.finditer(r"<([A-Z]\w*)((?:\s+\w+=\"[^\"]*\")*)\s*(/?)>", line):
            tag = match.group(1)
            attributes = re.findall(r"(\w+)=\"([^\"]*)\"", match.group(2))
            if not attributes and not match.group(3):
                continue
            if tag == "Task":
                if dict(attributes).get("class") != xml_class:
                    continue
                for name, value in attributes:
                    if name not in TASK_STANDARD_ATTRIBUTES:
                        entry = task_attributes.setdefault(name, {"examples": []})
                        if value and value != "...":
                            add_example(entry["examples"], value)
                continue
            if tag in STRUCTURE_TAGS:
                continue
            setting = elements.setdefault(tag, {})
            for name, value in attributes:
                entry = setting.setdefault(name, {"examples": []})
                if value and value != "...":
                    add_example(entry["examples"], value)
    return elements, task_attributes


def scan_trees(statetree):
    """task class -> {"settings": {tag: {...}}, "taskAttributes": {...}, "uses": n}"""
    seen = {}
    for directory, _, names in os.walk(statetree):
        for name in sorted(names):
            if not (name.startswith("STAI_") and name.endswith(".xml")):
                continue
            try:
                root = ET.parse(os.path.join(directory, name)).getroot()
            except ET.ParseError as error:
                print("warning: %s: %s" % (os.path.join(directory, name), error), file=sys.stderr)
                continue
            for task in root.iter("Task"):
                cls = task.get("class")
                if not cls:
                    continue
                info = seen.setdefault(cls, {"settings": {}, "taskAttributes": {}, "uses": 0})
                info["uses"] += 1
                for key, value in task.attrib.items():
                    if key in TASK_STANDARD_ATTRIBUTES:
                        continue
                    add_example(info["taskAttributes"].setdefault(key, {"examples": []})["examples"], value)
                counts = {}
                for child in task:
                    if not isinstance(child.tag, str) or child.tag in TASK_STRUCTURE_CHILDREN:
                        continue
                    counts[child.tag] = counts.get(child.tag, 0) + 1
                    setting = info["settings"].setdefault(child.tag, {"attributes": {}, "repeat": False})
                    for key, value in child.attrib.items():
                        add_example(setting["attributes"].setdefault(key, {"examples": []})["examples"], value)
                for tag, count in counts.items():
                    if count > 1:
                        info["settings"][tag]["repeat"] = True
    return seen


def merge_attribute(target, source):
    for value in source.get("examples", []):
        add_example(target.setdefault("examples", []), value)
    for option in source.get("options", []):
        options = target.setdefault("options", [])
        if option not in options:
            options.append(option)


def build(repo):
    state_tree_dir = os.path.join(repo, "playerbot", "StateTree")
    statetree = os.path.join(repo, "statetree")
    sources = Sources(state_tree_dir)
    readme = readme_table(os.path.join(statetree, "README.md"))
    node_docs = spec_node_table(os.path.join(statetree, "STATETREE_FORMAT.md"))
    trees = scan_trees(statetree)

    empty_types = {"ObjectGuid"}
    for name, info in sources.classes.items():
        if re.search(r"\b(?:IsEmpty|empty)\s*\(\s*\)\s*const", info["body"]):
            empty_types.add(name)

    used_types = set()
    tasks = {}
    for kind, xml_class, cpp_class in sources.registrations:
        if kind != "Task":
            continue
        ancestors = sources.ancestors(cpp_class)
        is_global = xml_class.endswith("_GlobalTask") or cpp_class in GLOBAL_BASES or bool(GLOBAL_BASES & set(ancestors))
        entry = {"cppClass": cpp_class, "global": is_global}
        info = sources.classes.get(cpp_class)
        if info:
            entry["header"] = info["file"]
            entry["base"] = info["bases"][0] if info["bases"] else ""

        properties = []
        entries, defined_in = sources.defs(cpp_class, "GetProperties")
        for name, struct, member in entries:
            type_name = sources.member_type(struct.split("::")[-1], member) or "?"
            used_types.add(type_name)
            properties.append({"name": name, "type": type_name,
                               "traits": type_traits(type_name, sources.aliases, empty_types)})
        if properties and defined_in != cpp_class:
            entry["propertiesFrom"] = defined_in
        delegates = []
        entries, defined_in = sources.defs(cpp_class, "GetDelegates")
        for name, struct, member in entries:
            delegates.append(name)
        if delegates and defined_in != cpp_class:
            entry["delegatesFrom"] = defined_in
        entry["properties"] = properties
        entry["delegates"] = delegates

        settings_help, does = readme.get(xml_class, ("", ""))
        if not does:
            does = sources.class_comment(cpp_class)
        entry["description"] = short_description(does)
        entry["doc"] = re.sub(r"\s+", " ", does).strip()
        entry["settingsHelp"] = "" if settings_help == "none" else settings_help

        readme_elements, readme_attributes = settings_from_readme(settings_help)
        seen = trees.get(xml_class, {"settings": {}, "taskAttributes": {}, "uses": 0})
        settings = {}
        for tag, setting in seen["settings"].items():
            settings[tag] = {"attributes": {k: dict(v) for k, v in setting["attributes"].items()},
                             "repeat": setting["repeat"], "seen": True}
        for tag, attributes in readme_elements.items():
            setting = settings.setdefault(tag, {"attributes": {}, "repeat": False, "seen": False})
            for name, attribute in attributes.items():
                merge_attribute(setting["attributes"].setdefault(name, {"examples": []}), attribute)
            if re.search(r"`<%s\b[^`]*`\s*per\b" % re.escape(tag), settings_help):
                setting["repeat"] = True
        task_attributes = {k: dict(v) for k, v in seen["taskAttributes"].items()}
        for name, attribute in readme_attributes.items():
            merge_attribute(task_attributes.setdefault(name, {"examples": []}), attribute)
        comment_elements, comment_attributes = settings_from_comment(sources.class_comment_lines(cpp_class), xml_class)
        for tag, attributes in comment_elements.items():
            setting = settings.setdefault(tag, {"attributes": {}, "repeat": False, "seen": False})
            for name, attribute in attributes.items():
                merge_attribute(setting["attributes"].setdefault(name, {"examples": []}), attribute)
        for name, attribute in comment_attributes.items():
            merge_attribute(task_attributes.setdefault(name, {"examples": []}), attribute)
        entry["taskAttributes"] = task_attributes
        entry["settings"] = settings
        entry["uses"] = seen["uses"]
        tasks[xml_class] = entry

    def node_classes(defined, kind):
        out = {}
        for cls, attributes in defined.items():
            attribute_doc, does = node_docs.get(cls, ("", ""))
            out[cls] = {"attributes": attributes, "description": short_description(does), "doc": does,
                        "attributesHelp": attribute_doc, "fromSource": False}
            if cls in CONSIDERATION_CHILDREN:
                out[cls]["children"] = CONSIDERATION_CHILDREN[cls]
        for registered_kind, xml_class, cpp_class in sources.registrations:
            if registered_kind != kind:
                continue
            if xml_class in out:
                out[xml_class]["fromSource"] = True
                out[xml_class]["cppClass"] = cpp_class
            else:
                attribute_doc, does = node_docs.get(xml_class, ("", ""))
                out[xml_class] = {"attributes": [], "description": short_description(does), "doc": does,
                                  "attributesHelp": attribute_doc, "fromSource": True, "cppClass": cpp_class}
        for cls, (attribute_doc, does) in node_docs.items():
            if cls.startswith(kind + "_") and cls not in out:
                out[cls] = {"attributes": [], "description": short_description(does), "doc": does,
                            "attributesHelp": attribute_doc, "fromSource": False}
        return dict(sorted(out.items()))

    aliases = {name: value for name, value in sorted(sources.aliases.items()) if name in used_types}
    return {
        "generatedBy": "tools/StateTreeEditor/gen_schema.py",
        "formatVersion": 2,
        "tasks": dict(sorted(tasks.items())),
        "conditions": node_classes(CONDITIONS, "Condition"),
        "considerations": node_classes(CONSIDERATIONS, "Consideration"),
        "typeAliases": aliases,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo", default=DEFAULT_REPO, help="the playerbots directory (default: ../.. of this script)")
    parser.add_argument("--out", default=os.path.join(HERE, "schema.json"))
    args = parser.parse_args()

    schema = build(args.repo)
    text = json.dumps(schema, indent=2, ensure_ascii=False) + "\n"
    with open(args.out, "w", encoding="utf-8", newline="\n") as file:
        file.write(text)

    tasks = schema["tasks"]
    missing = ["%s::%s" % (cls, p["name"]) for cls, t in tasks.items() for p in t["properties"] if p["type"] == "?"]
    print("%s: %d task classes (%d global), %d conditions, %d considerations"
          % (args.out, len(tasks), sum(1 for t in tasks.values() if t["global"]), len(schema["conditions"]),
             len(schema["considerations"])))
    if missing:
        print("warning: property types not found: " + ", ".join(missing), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
