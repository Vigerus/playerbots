# State tree host test

Builds the state tree core of `playerbot/StateTree` (tree asset, loader, runtime instance, base classes, conditions,
considerations, `Task_Wait`, `Task_RunSubtree`, the framework tasks) as a small Windows console program, without the
server and without boost: `stubs/` stands in for the PCH, the logging, the delegates and a minimal
`boost::property_tree` with an XML reader. The game's task classes are replaced by stubs generated from
`../StateTreeEditor/schema.json` (`gen_stubs.py`): same properties, delegates and types, random results and values.

```bat
tools\StateTreeHostTest\run.bat
```

(MSVC 2022, its `vcvars64.bat` at the default Professional path or given as the first argument; Python 3.)

It checks:

- `tests/test_*.xml`: the runtime's semantics with deterministic test tasks: a sequence through `NextState`, utility
  selection, an `OnEvent` transition into a `Linked` subtree whose `Succeeded` target completes the linked state, a
  failed state waiting its retry delay, a delayed transition, an `OnDelegate` transition and
  `Condition_ActiveStateLocation`.
- every `statetree/**/STAI_*.xml` loads through `STAI_TreeLibrary::LoadFromConfig` without an error (paths, bindings,
  transition targets, linked subtrees), and runs 400 updates with the random stubs (enter / exit calls balance).

It does not replace a server build: the game's tasks and providers are not compiled here.
