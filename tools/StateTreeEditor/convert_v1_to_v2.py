"""
Converts STAI state tree files from the v1 format (<Root>, <Global>, <Node>) to the v2 format (<StateTree version="2">,
<GlobalTasks>, <State>) read by the server since the RedStateTree-like refactor (see statetree/README.md).

- XML comments in front of a <Node> / <Task> become its <Comment> (developer comment), comments in front of <Root>
  the tree's <Comment>; comments inside a task's settings stay where they are.
- <Conditions> of a node -> <EnterConditions> (left out when empty), <Condition> gets class="Condition_Compare",
  negate="true" -> inverted="true".
- Node tasks go into <Tasks>. A node with several tasks (they ran one after another) becomes a parent state with one
  child state per task, each but the last going on with <Transition trigger="OnStateSucceeded" target="NextState"/>.
- <Transition type="OnSuccess|OnFailure|OnFinish|OnDelegate" node="..."> -> trigger="OnStateSucceeded|OnStateFailed|
  OnStateCompleted|OnDelegate" target="...".

Usage:
    python convert_v1_to_v2.py <statetree dir or file> [...]     converts in place (files already in v2 are skipped)
    python convert_v1_to_v2.py --check <dir>                     only lists the files still in v1
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape, quoteattr

INDENT = "  "

TRIGGERS = {
    "OnSuccess": "OnStateSucceeded",
    "OnFailure": "OnStateFailed",
    "OnFinish": "OnStateCompleted",
    "OnDelegate": "OnDelegate",
}


def parse(path):
    builder = ET.TreeBuilder(insert_comments=True)
    parser = ET.XMLParser(target=builder)
    with open(path, encoding="utf-8") as file:
        text = file.read()
    parser.feed(text)
    return text, parser.close()


def leading_comments(text):
    """comments between the xml declaration and the root element (ElementTree does not keep them)"""
    head = text.split("<Root", 1)[0]
    return [comment.strip() for comment in re.findall(r"<!--(.*?)-->", head, re.S)]


def comment_text(raw):
    lines = [line.strip() for line in raw.strip().splitlines()]
    return "\n".join(line for line in lines if line)


def element(tag, attrib=None, text=None):
    node = ET.Element(tag, attrib or {})
    if text is not None:
        node.text = text
    return node


def add_comment(target, comments):
    """<Comment> as the first child of target"""
    if not comments:
        return
    target.insert(0, element("Comment", text="\n".join(comment_text(c) for c in comments)))


def convert_condition(condition):
    attrib = {"class": "Condition_Compare"}
    for key, value in condition.attrib.items():
        if key == "negate":
            if value in ("true", "1"):
                attrib["inverted"] = "true"
            continue
        attrib[key] = value
    return element("Condition", attrib)


def convert_conditions(conditions, tag):
    out = element(tag, {k: v for k, v in conditions.attrib.items() if not (k == "match" and v == "all")})
    for child in conditions:
        if child.tag is ET.Comment:
            continue
        if child.tag == "Condition":
            out.append(convert_condition(child))
        elif child.tag == "Any":
            any_group = element("Any")
            for member in child:
                if member.tag == "Condition":
                    any_group.append(convert_condition(member))
            out.append(any_group)
        else:
            raise ValueError("unexpected <%s> in <Conditions>" % child.tag)
    return out


def convert_task(task, comments, global_task):
    out = element("Task", dict(task.attrib))
    add_comment(out, comments)
    for child in task:
        if child.tag == "Conditions":
            if len([c for c in child if c.tag is not ET.Comment]):
                out.append(convert_conditions(child, "Conditions"))
            continue
        # settings, bindings and the comments between them stay as they are
        out.append(child)
    return out


def convert_transition(transition):
    attrib = {}
    kind = transition.get("type")
    if kind not in TRIGGERS:
        raise ValueError("unknown transition type '%s'" % kind)
    attrib["trigger"] = TRIGGERS[kind]
    if transition.get("source"):
        attrib["source"] = transition.get("source")
    attrib["target"] = transition.get("node", "Root")
    out = element("Transition", attrib)
    for child in transition:
        if child.tag == "Conditions" and len([c for c in child if c.tag is not ET.Comment]):
            out.append(convert_conditions(child, "Conditions"))
    return out


def convert_node(node, comments, used_names):
    state = element("State", {"name": node.get("name") or next(t.get("class") for t in node if t.tag == "Task")})
    add_comment(state, comments)

    tasks = []          # (task element, its comments)
    pending = []
    transitions = None
    for child in node:
        if child.tag is ET.Comment:
            pending.append(child.text)
        elif child.tag == "Conditions":
            if len([c for c in child if c.tag is not ET.Comment]):
                state.append(convert_conditions(child, "EnterConditions"))
            if pending:
                add_comment(state, pending)
                pending = []
        elif child.tag == "Task":
            tasks.append((child, pending))
            pending = []
        elif child.tag == "Transitions":
            transitions = element("Transitions")
            for transition in child:
                if transition.tag == "Transition":
                    transitions.append(convert_transition(transition))
        else:
            raise ValueError("unexpected <%s> in <Node>" % child.tag)

    if len(tasks) == 1:
        group = element("Tasks")
        group.append(convert_task(tasks[0][0], tasks[0][1], False))
        state.append(group)
        if transitions is not None:
            state.append(transitions)
        return state

    # tasks that ran one after another: a child state per task, in order
    if transitions is not None:
        state.append(transitions)
    state.set("retryDelay", "3000")
    for index, (task, task_comments) in enumerate(tasks):
        name = "%s %s" % (state.get("name"), task.get("class").replace("Task_", ""))
        while name in used_names:
            name += "'"
        used_names.add(name)
        child = element("State", {"name": name})
        group = element("Tasks")
        group.append(convert_task(task, task_comments, False))
        child.append(group)
        if index + 1 < len(tasks):
            child_transitions = element("Transitions")
            child_transitions.append(element("Transition", {"trigger": "OnStateSucceeded", "target": "NextState"}))
            child.append(child_transitions)
        state.append(child)
    return state


def convert(text, root):
    tree = element("StateTree", {"version": "2"})
    add_comment(tree, leading_comments(text))

    used_names = {node.get("name") for node in root if node.tag == "Node"}
    pending = []
    for child in root:
        if child.tag is ET.Comment:
            pending.append(child.text)
        elif child.tag == "Global":
            if pending:
                # in front of <Global>: about the tree
                comment = tree.find("Comment")
                extra = "\n".join(comment_text(c) for c in pending)
                if comment is None:
                    add_comment(tree, pending)
                else:
                    comment.text += "\n" + extra
                pending = []
            group = element("GlobalTasks")
            task_comments = []
            for task in child:
                if task.tag is ET.Comment:
                    task_comments.append(task.text)
                elif task.tag == "Task":
                    group.append(convert_task(task, task_comments, True))
                    task_comments = []
            tree.append(group)
        elif child.tag == "Node":
            tree.append(convert_node(child, pending, used_names))
            pending = []
        else:
            raise ValueError("unexpected <%s> in <Root>" % child.tag)

    if pending:
        comment = tree.find("Comment")
        extra = "\n".join(comment_text(c) for c in pending)
        if comment is None:
            add_comment(tree, pending)
        else:
            comment.text += "\n" + extra
    return tree


def write_element(node, depth, out):
    pad = INDENT * depth
    if node.tag is ET.Comment:
        out.append("%s<!--%s-->" % (pad, node.text))
        return

    attrib = "".join(" %s=%s" % (key, quoteattr(value)) for key, value in node.attrib.items())
    children = list(node)
    text = (node.text or "").strip()

    if node.tag == "Comment":
        lines = text.split("\n")
        body = ("\n" + pad + INDENT).join(escape(line) for line in lines)
        out.append("%s<Comment>%s</Comment>" % (pad, body))
        return

    if not children and not text:
        out.append("%s<%s%s/>" % (pad, node.tag, attrib))
        return
    if not children:
        out.append("%s<%s%s>%s</%s>" % (pad, node.tag, attrib, escape(text), node.tag))
        return

    out.append("%s<%s%s>" % (pad, node.tag, attrib))
    for child in children:
        write_element(child, depth + 1, out)
    out.append("%s</%s>" % (pad, node.tag))


def serialize(tree):
    out = ['<?xml version="1.0" encoding="utf-8"?>']
    write_element(tree, 0, out)
    return "\n".join(out) + "\n"


def files_of(paths):
    for path in paths:
        if os.path.isdir(path):
            for directory, _, names in os.walk(path):
                for name in sorted(names):
                    if name.startswith("STAI_") and name.endswith(".xml"):
                        yield os.path.join(directory, name)
        else:
            yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+")
    parser.add_argument("--check", action="store_true", help="only list the files still in the v1 format")
    args = parser.parse_args()

    converted = 0
    for path in files_of(args.paths):
        text, root = parse(path)
        if root.tag != "Root":
            continue
        if args.check:
            print(path)
            continue
        result = serialize(convert(text, root))
        with open(path, "w", encoding="utf-8", newline="\r\n") as file:
            file.write(result)
        converted += 1
        print("converted", path)
    if not args.check:
        print("%d file(s) converted" % converted)


if __name__ == "__main__":
    sys.exit(main())
