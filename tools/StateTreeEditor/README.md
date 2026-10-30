# State Tree Editor

A browser based editor for the bot state trees (`statetree/**/STAI_*.xml`, format v2, see
`statetree/STATETREE_FORMAT.md`), laid out like the Unreal Engine / RedStateTree State Tree asset editor. Python 3
standard library and plain HTML / CSS / JavaScript: no install, no build step, no external fonts or CDNs.

## Run

```
python tools/StateTreeEditor/serve.py --open
```

The server binds to `127.0.0.1` only and prints the URL (default `http://127.0.0.1:8765/`).

| option | default | |
|--------|---------|-|
| `--port` | `8765` | |
| `--root` | `../../statetree` (relative to the script) | the directory of the tree files |
| `--eol` | `auto` | line endings of saved files: `auto` keeps the file's own (CRLF for new files), `crlf`, `lf` |
| `--open` | | opens the editor in the browser |
| `--verbose` | | logs every request |

## What it does

- **Files** (left): the tree files of the root by folder (class folders), a filter box, a dot on modified files and
  the error count of the opened ones. New creates a tree from a minimal template, Save writes the file (refused when
  it changed on disk since it was loaded, you can overwrite), Revert reloads it. Every opened file keeps its own
  changes and undo history until the page is closed (the browser warns about unsaved changes).
- **Outline** (center): the tree's comment, the Global Tasks, then the states, nested and collapsible. A state row
  shows its selection behaviour icon, name, type (Subtree / Linked badge), utility (`U`: weight and considerations),
  enter conditions as chips, its tasks (class and a short info: spell, delay, subtree...) and below it its
  transitions (`trigger -> target`). Disabled states / tasks / transitions are dimmed, comments are tooltips (the pen
  mark). Click selects, right click opens the context menu (add child / sibling state, add task, transition,
  condition, consideration, duplicate, move up / down, enable / disable, delete). Drag a state onto the upper / lower
  edge of another state to put it before / after it, onto its middle to make it a child; drag a task onto a state, a
  task chip or the Global Tasks header. Moves and renames keep the paths, targets and `linkedSubtree` that point to the
  moved / renamed state or task working.
- **Details** (right): the selected tree, state, global task, task, transition, condition (group) or consideration,
  every attribute of the format with dropdowns for the enums, comment fields, pickers for `linkedSubtree` (the Subtree
  states), `target` (Root, NextState, Succeeded, Failed, every state), `source` (every delegate of the tree's tasks)
  and `binding` / `external` (every property, a binding's list filtered to the type of its local property; the pen
  button edits a path as text). Conditions are edited in place (nested Any / All groups, NOT, wrap in a group, move,
  delete), considerations too (Number: binding, min, max and the curve keys with a plot). A task shows its
  documentation, its bindings, a form for the settings known from the schema and the raw XML of all its settings
  (every child but `Comment` / `Binding` / `Conditions`, XML comments included) for anything else. Attributes the
  editor does not know are listed under "Other attributes" and can be edited, added and removed.
- **Problems** (bottom): live validation following the server loader: unique state names, unknown task / condition /
  consideration classes, paths (state, task class in that state or the globals, `[name]`, the member is a property /
  delegate of the class), binding types, the condition checks of `Condition_Compare` (set test, number / text compare,
  `key` on a map of numbers), required attributes, `Linked` needs a `Subtree` state, leaf states need a task,
  `NextState` needs a next sibling, `OnDelegate` needs a delegate `source`, `OnEvent` an `event`,
  `Task_RunSubtree` files exist (warning), `<Conditions>` only on global tasks, ... Click a problem to select it.
- **XML** (toolbar): the source of the tree as it will be saved; edit it and Apply (Ctrl+Enter). A file that does not
  parse opens there.

The model is the XML document itself: elements, attributes and XML comments the editor does not know are kept as
they are. Files are written with the formatting of `convert_v1_to_v2.py` (2 spaces, `<?xml version="1.0"
encoding="utf-8"?>`, self-closing empty elements, `<Comment>` continuation lines indented one level deeper): saving a
file that was not changed reproduces it byte for byte (line endings aside).

## Keyboard

| key | |
|-----|-|
| Ctrl+S | save |
| Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) | undo / redo |
| Ctrl+P | filter the files |
| Ctrl+F | find a state / task in the outline (Enter: next match) |
| Ctrl+Shift+X | XML source view |
| Up / Down | previous / next row of the outline |
| Left / Right | collapse / expand, go to the parent / first child |
| Alt+Up / Alt+Down | move the selection up / down among its siblings |
| Ins / Shift+Ins | add a child / sibling state |
| T | add a task to the selected state (or a global task to the tree) |
| Ctrl+D | duplicate |
| Del | delete |
| Ctrl+E | enable / disable |
| I | invert the selected condition |
| F2 | rename the selected state |
| Esc | close menus, select the parent |
| F1 | the shortcuts |

## Schema

The editor reads the task, condition and consideration classes from `schema.json`, built by

```
python tools/StateTreeEditor/gen_schema.py
```

from `playerbot/StateTree` (the `RegisterTask` / `RegisterCondition` / `RegisterConsideration` calls, the
`GetProperties` / `GetDelegates` of the classes, inherited from the base class when not overridden, and the member
types of their context structs), the settings used in the trees, the task table of `statetree/README.md` (descriptions,
settings) and the node classes table of `statetree/STATETREE_FORMAT.md`. Run it again after adding or changing a task,
a property or a delegate.

## Round trip test

`http://127.0.0.1:8765/?selftest=1` loads every tree file through the API, parses and writes it again, compares the
result with the file (line endings normalized), runs a few edit round trips and the validation, and lists the
mismatches and problems per file (the page title says `SELFTEST PASS` or `SELFTEST FAIL`; `window.selftestResult`
holds the details).

## v1 files

The server only reads v2. Convert v1 files (`<Root>`, `<Global>`, `<Node>`) with

```
python tools/StateTreeEditor/convert_v1_to_v2.py statetree            converts in place, v2 files are skipped
python tools/StateTreeEditor/convert_v1_to_v2.py --check statetree    lists the files still in v1
```

## API

| request | |
|---------|-|
| `GET /api/files` | `{"root", "files": [{"path", "size", "mtime"}], "folders"}`, paths relative to the root |
| `GET /api/file?path=<rel>` | the file's text, header `X-Mtime` |
| `PUT /api/file?path=<rel>` (or `POST`) | saves the body atomically; with header `X-Base-Mtime` refused (409) when the file changed since, `&force=1` overwrites |
| `POST /api/new?path=<rel>` | creates the file from the body (empty: a minimal tree); 409 when it exists |
| `GET /api/schema` | `schema.json` |

Only `*.xml` files under the root: absolute paths, `..`, `.` and empty segments are refused.

## Future work

- Live view: highlight the active states of a bot (the server's map endpoint, `PlayerbotMapServer`, already sends
  every tree with `depth` / `parent` per state) and toggle the addon switches (tasks, states, conditions) from here.
- A server command to reload the trees after a save (the server reads them only at start).
- Required settings per task class in the schema (`Task_CastSpell` without `spellid` is not reported yet), ideally
  dumped by the server from the node factory instead of `gen_schema.py` scanning the sources.
- Property functions and tree parameters, once the runtime has them (see `statetree/STATETREE_TODO.md`).
