# STAI state tree files, format v2

The `STAI_*.xml` files of this directory (and its class folders) describe the state trees the bots run. The format
follows the structure of a RedStateTree / UE StateTree asset: a tree of **states**, each with **enter conditions**,
**considerations** (utility), **tasks** and **transitions**, plus **global tasks** (data providers) that run the whole
time the tree runs. The server loads them at start (`STAI_TreeLibrary`), the editor in `tools/StateTreeEditor` edits
them. v1 files (`<Root>`, `<Global>`, `<Node>`) are converted with `tools/StateTreeEditor/convert_v1_to_v2.py`; the
server only reads v2.

```xml
<?xml version="1.0" encoding="utf-8"?>
<StateTree version="2">
  <Comment>what the tree is for</Comment>
  <GlobalTasks>
    <Task class="BuffProvider_GlobalTask">
      <Comment>the buffs the group misses: needs, onMissingBuff</Comment>
      <Buff spellid="1459"/>
    </Task>
    <Task class="MovementGoal_GlobalTask" name="Follow Master" priority="10" kind="location">
      <Radius value="follow"/>
      <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
      <Conditions>
        <Condition class="Condition_Compare" binding="Root|MasterDistance_GlobalTask::masterFar"/>
      </Conditions>
    </Task>
  </GlobalTasks>
  <State name="Check Buffs">
    <EnterConditions>
      <Condition class="Condition_Compare" binding="Root|BuffProvider_GlobalTask::needs"/>
    </EnterConditions>
    <Tasks>
      <Task class="Task_CastBuff">
        <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>
      </Task>
    </Tasks>
  </State>
  <State name="Idle">
    <Tasks>
      <Task class="Task_Wait" show_log="false">
        <Delay value="0"/>
      </Task>
    </Tasks>
    <Transitions>
      <Transition trigger="OnDelegate" source="Root|BuffProvider_GlobalTask::onMissingBuff" target="Root|Check Buffs"/>
    </Transitions>
  </State>
</StateTree>
```

## Elements

`<Comment>` (developer comment, RedStateTree's `DeveloperComment`) is allowed as the first child of `<StateTree>`,
`<State>`, `<Task>`, `<Transition>`, `<Condition>` and `<Consideration>`; the server ignores it, the editor shows it.
Plain XML comments inside a task's settings stay where they are (the server ignores them too).

### `<StateTree version="2">`

The root element. Children: `<Comment>`, at most one `<GlobalTasks>`, then the top level `<State>`s (at least one).
The top level states are the children of the implicit **Root** state: paths start with `Root|`.

### `<GlobalTasks>`

`<Task>`s (normally data providers, `*_GlobalTask`) that belong to no state: entered when the tree starts (in order),
ticked on every update of the tree before the states, exited when it stops (in reverse order). A global task may have
its own `<Conditions>`: they are tested before every update of the task and set its context's `bActive`
(`MovementGoal_GlobalTask` only wants its goal while they pass).

### `<State>`

| attribute | default | meaning |
|-----------|---------|---------|
| `name` | required | unique in the tree (also across nesting levels); paths use it |
| `type` | `State` | `State`; `Subtree`: only entered through a `Linked` state or a transition, never by the selection of its parent; `Linked`: runs the `Subtree` state named by `linkedSubtree` in its place (its children are the subtree's children) |
| `linkedSubtree` | | `Linked` only: name of a state of type `Subtree` of the same tree |
| `selection` | `TrySelectChildrenInOrder` | how a child is selected when the state is entered: `TrySelectChildrenInOrder` (first child whose enter conditions pass), `TrySelectChildrenAtRandom`, `TrySelectChildrenWithHighestUtility` (children by their utility, highest first), `TrySelectChildrenAtRandomWeightedByUtility`, `TryFollowTransitions` (the first of the state's own transitions to a state below it, trigger neither OnDelegate nor OnEvent, whose conditions pass and whose target can be entered) |
| `weight` | `1` | utility weight: the utility of the state is `weight` times the product of its considerations' scores; the utility selections skip states of utility 0 |
| `retryDelay` | `3000` for states without children, `0` else | ms: a state that failed is not selected again by the selection for that long (explicit transitions to it ignore it) |
| `enabled` | `true` | `false`: never selected, no transition goes to it |

Children, in this order: `<Comment>`, `<EnterConditions>`, `<Considerations>`, `<Tasks>`, `<Transitions>`, child
`<State>`s. A state without child states needs at least one task (except `Linked`).

### `<EnterConditions match="all|any">` and `<Conditions match="all|any">`

`<EnterConditions>` of a state, `<Conditions>` of a transition or a global task. They pass when all (default) or any
of their entries pass; empty or missing always passes. Entries:

- `<Condition class="..." inverted="false" ...>`: a condition node (below); `inverted="true"` negates its result
  (RedStateTree's `bInverted`).
- `<Any> ... </Any>` / `<All> ... </All>`: a group, passes when one / all of its entries pass; groups nest.

### `<Considerations>` (utility)

`<Consideration class="..." .../>` entries; their scores (0..1) are multiplied with the state's `weight` into its
utility, used by the `...Utility` selections of the parent. A state without considerations has the utility `weight`.

### `<Tasks>`

The state's `<Task>`s. All tasks of the active states run at the same time (RedStateTree semantics): a parent state's
tasks keep running while its children run. A task finishing completes its state according to its `completion`:

| attribute | default | meaning |
|-----------|---------|---------|
| `class` | required | a registered task class |
| `name` | | picks the task in paths: `Root|<TaskClass>[<name>]::<member>` |
| `completion` | `AlwaysFinish` (global tasks: `IgnoreResult`) | RedStateTree's `ETaskType`: `AlwaysFinish` (the task finishing completes the state with its result), `FinishWhenSucceeded`, `FinishWhenFailed`, `IgnoreResult` (the state keeps running, the task stays finished) |
| `show_log` | task default | `false`: no History entries for its runs |
| `enabled` | `true` | `false`: the task is left out (the debug addon can switch tasks off at runtime too) |

Children: `<Comment>`, the task's own settings (see the task tables in `README.md`), `<Binding local="<property>"
external="<path>"/>` (the external property is copied into the task's own property, same type, before its enter and
every tick), `<Conditions>` (global tasks only).

### `<Transitions>`

| attribute | default | meaning |
|-----------|---------|---------|
| `trigger` | required | `OnStateCompleted`, `OnStateSucceeded`, `OnStateFailed` (the active states completed), `OnTick` (every update while its conditions pass), `OnEvent` (an event with the tag `event` was sent to the tree this update), `OnDelegate` (the delegate `source` fired) |
| `target` | required | `Root` (select again from the top), `Root|<state name>` (or the full path `Root|<parent>|<state>`), `NextState` (the next sibling of the transition's state), `Succeeded` / `Failed` (the tree, or the `Linked` state running the subtree, completes) |
| `source` | | `OnDelegate`: `Root|<TaskClass>::<delegate>` / `Root|<state>|<TaskClass>::<delegate>` |
| `event` | | `OnEvent`: the event tag |
| `priority` | `Normal` | `Low`, `Normal`, `Medium`, `High`, `Critical`: of the transitions triggered in one update the highest priority wins, then the deepest state, then file order |
| `delay` | `0` | ms: the transition is taken that long after it triggered, while its state stays active |
| `enabled` | `true` | |

Children: `<Comment>`, `<Conditions>` (the transition is only taken while they pass).

Transitions are checked from the leaf state up to the top level state. Completion transitions of every active state
see the completion; none matching: the tree selects again from Root. An `OnDelegate` delegate fired while no active
state has a transition on it is remembered until one does.

## Paths

- `Root|<TaskClass>::<member>`: a property / delegate of a global task.
- `Root|<state name>|<TaskClass>::<member>`: of a task of that state (the full path of nested states works too).
- `<TaskClass>[<task name>]` picks the task with that `name`.

## Node classes

Tasks: see `README.md`. Conditions and considerations (`playerbot/StateTree/Conditions`, `Considerations`):

| class | attributes | does |
|-------|------------|------|
| `Condition_Compare` | `binding` (path, required), `compare` (`equal`, `lesser`, `lesser or equal`, `greater`, `greater or equal`), `value`, `key` | without `compare`: passes while the property is set (a container not empty, else its bool value); with `compare`: compares a number property (or a text with `equal`); `key`: the entry of a map of numbers instead |
| `Condition_RandomChance` | `chance` (percent, 0..100) | passes with that chance on every test |
| `Condition_ActiveStateLocation` | `location` (`above`, `this`, `below`, comma separated; default `above,below`: "don't reenter") | where the active leaf state is relative to the state the condition belongs to (RedStateTree's States Order) |
| `Condition_TimeInState` | `ms` (required) | passes once the state the condition belongs to has been active for that long |
| `Consideration_Constant` | `value` (0..1) | a constant score |
| `Consideration_Number` | `binding` (number path, required), `min` (0), `max` (1), `<Key x="0" y="0"/>` children (a piecewise linear curve over the normalized input; none: the normalized input) | normalizes the bound number into 0..1 and maps it through the curve |

The editor reads the node classes, their settings, properties and delegates from `tools/StateTreeEditor/schema.json`
(`gen_schema.py` builds it from the sources and the trees). `tools/StateTreeHostTest/run.bat` loads and runs every
tree without a server. Future work: [STATETREE_TODO.md](STATETREE_TODO.md).
