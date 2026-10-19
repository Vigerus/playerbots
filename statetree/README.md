# State trees

Each bot state (NonCombat, Combat, Reaction, Dead) runs a state tree built when the bot is loaded:

- `STAI_<State>[_<Class>].xml`: the tree. Nodes with conditions and a task.
- `Legacy_<State>[_<Class>].json`: config of the `StateNode_Legacy` / `StateNode_Reaction` task, the strategy
  list of the legacy engine (replaces `AiFactory::AddDefault<State>Strategies`).

Configured in `aiplayerbot.conf` (paths relative to its directory):

```
AiPlayerbot.StateTree.<State>.Default = statetree/STAI_<State>.xml
AiPlayerbot.StateTree.<State>.<Class> = statetree/STAI_<State>_<Class>.xml
```

`<Class>` is Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid (DeathKnight on WotLK).
The class entry wins over Default. An empty value, or a file that fails to load (the error is in the server log
at startup), gives the state a single legacy node with the built-in AiFactory strategies. Files are read at
server start.

The StateTree debug addon shows each state's tree nodes, the active one highlighted.

## STAI tree (`STAI_*.xml`)

```xml
<Root>
  <Node name="Follow player">
    <Conditions>
      <Condition trigger="has strategy::follow"/>
      <Condition trigger="has strategy::stay" negate="true"/>
      <Condition trigger="distance from master::greater::follow"/>
    </Conditions>
    <Task class="Task_Follow">
      <Target value="player"/>
      <AcceptableRadius value="follow"/>
    </Task>
  </Node>
  <Node name="Legacy">
    <Conditions/>
    <Task class="StateNode_Legacy">
      <Config filename="statetree/Legacy_NonCombat_Mage.json"/>
    </Task>
  </Node>
</Root>
```

There is always a single active path from the tree to the running node. A node is selected only while none runs
(the tree started, the node finished, a transition went to `Root`): the first node from the top whose conditions all
pass is entered (`OnEnter`); conditions are only checked then. Its task is updated every bot update until it
returns Succeeded or Failed (`OnExit`, the next update selects again), or until an `OnDelegate` transition of the node
fires. Nothing else leaves a running node: a node that never finishes (Legacy) needs `OnDelegate` transitions, for
example to Follow. A node whose task failed is not selected for 3 seconds. Empty `<Conditions/>` always pass, so a
last unconditional node is the fallback.

Conditions are the bot's triggers, by the same names (and `::` qualifiers) as in strategies and custom strategies,
for example `critical health`, `far from master` or `has aura::spellid::23341::greater or equal::5`. A condition
passes while its trigger is active, or while it is inactive with `negate="true"`. `<Condition binding="Root|..."/>`
tests a task property instead (see below): it passes while the property is set (a container not empty, else its
bool value). Triggers are looked up per bot
(class triggers included); an unknown name is logged when the bot loads and never passes. The debug addon shows each
condition green (passed), red (failed) or gray (not checked in the last update).

Triggers added for trees:

| trigger | active when |
|---------|-------------|
| `distance from master::<comparison>::<yards or range>` | 2D distance to the master compared with `equal`, `greater`, `greater or equal`, `lesser`, `lesser or equal`; instead of yards a range name (`follow`, `spell`, ...) uses the bot's range (`range` command, else the config default, e.g. `AiPlayerbot.FollowDistance`); never without a master in the same map |
| `has strategy::<name>` | the engine of the bot's current state has the strategy |

Tasks:

| class                | settings | does |
|----------------------|----------|------|
| `StateNode_Legacy`   | `<Config filename="..."/>` (optional) | runs the legacy engine of the state with the Legacy_*.json strategy list (AiFactory defaults without Config); never finishes |
| `StateNode_Reaction` | `<Config filename="..."/>` (optional) | runs the reaction engine; lets the Reaction state preempt the others |
| `Task_Follow`        | `<Target value="player"/>`, `<AcceptableRadius value="follow"/>` (yards or a range name, default `follow`) | the only owner of follow movement: `Update` recomputes the spot next to the master and moves there again when it moved; Succeeded inside the radius once the master stands still; Failed when the master is gone, flying, on another map or transport, or after 30s outside the radius; `OnExit` stops its movement |
| `MasterDistance_GlobalTask` | `<Range value="follow"/>` (yards or a range name, default `follow`) | global task: property `masterFar`, delegate `onMasterFar` fired every update while the master is beyond the range |
| `BuffProvider_GlobalTask` | `<Buff spellid="..." scope="self\|single\|group"/>` per spell (any rank; scope optional, read from the spell's targets) | global task, not ticked: watches the buffs of the bot and its group (TeamSubsystem, SpellAuraWorldSubsystem); property `needs` (the casts to do), delegate `onMissingBuff` fired when an evaluation finds any; spells applying the same aura are one buff (Arcane Intellect / Arcane Brilliance) |
| `CurseProvider_GlobalTask` | `<Aura spellid="..."/>` per aura (any rank), optional | global task, not ticked: watches the bot and its group for the listed auras, or every dispellable curse when none is listed; property `whoNeeds` (the cursed members, the bot first), delegate `onNeedsHelp` fired when an evaluation finds any |
| `Task_CastSpell`     | `spellid="..."` on the `<Task>` (spell id, or a name: the bot's highest rank), `<Binding local="whoNeeds" external="..."/>` | casts the spell on the first `whoNeeds` target it can reach; waits for the cast and the GCD; Succeeded also when `whoNeeds` is empty, Failed when the bot does not know the spell or reaches no target |
| `Task_CastBuff`      | `<Binding local="needs" external="Root\|BuffProvider_GlobalTask::needs"/>` | casts one of its `needs`: self spell, single target spell for a member missing it alone, group spell when 2+ members of a subgroup miss it; waits for the cast and the GCD; Succeeded also when nothing is needed, Failed when nothing can be cast |

New tasks implement `STAI_Task` (`playerbot/StateTree/Tasks/`) and are registered in `STAI_BuiltinTasks.cpp`.

Global tasks (`<Global>` directly under `<Root>`) belong to no node: they are entered when the tree starts and exited
when it stops. Tasks expose properties and delegates of their context (`STAI_Task::GetProperties` / `GetDelegates`),
addressed by path: `Root|<TaskClass>::<name>` for a global task, `Root|<node name>|<TaskClass>::<name>` for a node's
task.

```xml
<Node name="Check Buffs">
  <Conditions> <Condition binding="Root|BuffProvider_GlobalTask::needs"/> </Conditions>
  <Task class="Task_CastBuff">
    <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>
  </Task>
</Node>
<Node name="Legacy">
  <Conditions/>
  <Task class="StateNode_Legacy"/>
  <Transitions>
    <Transition type="OnDelegate" source="Root|MasterDistance_GlobalTask::onMasterFar" node="Root|Follow player"/>
    <Transition type="OnDelegate" source="Root|BuffProvider_GlobalTask::onMissingBuff" node="Root|Check Buffs"/>
  </Transitions>
</Node>
```

- `<Binding local="..." external="...">` in a `<Task>`: the external property is copied into the task's own property
  (same type) before its `OnEnter` and every `OnTick`.
- `<Transition type="OnSuccess|OnFailure|OnFinish">` is taken when the node finished (none matching: the next update
  selects from the first node).
- `<Transition type="OnDelegate" source="...">` is taken when the source delegate fired: at the start of the next
  update, if the node is running and the target's conditions pass (otherwise the node keeps running). A delegate fired
  while no node with a transition on it runs is remembered until one does.
- `node="Root"` selects again from the first node, `Root|<name>` enters that node (its conditions still have to pass).

Unknown paths, properties of different types and delegates that do not exist stop the server when the trees load.
The class NonCombat trees follow the master through `MasterDistance_GlobalTask`. `STAI_NonCombat_Mage.xml` also
decurses and buffs: Legacy goes to Check Decurse when someone is cursed (Remove Lesser Curse) and to Check Missing
Buffs when a buff (Arcane Intellect / Arcane Brilliance, Mage Armor) is missing; each casts once per visit and is
selected again while its `whoNeeds` / `needs` is not empty. The `buff` strategy is not in the Legacy lists.

Activity history (History tab of the debug addon): every bot keeps the last minute of activity in its
`STAI_ActivityLog`, reachable by tasks through `STAI_TaskContext::activity`. The tree adds an entry from `OnEnter` to
`OnExit` of each node (tasks return `false` from `LogsNodeActivity()` to opt out, like the legacy nodes). Tasks add
their own entries with `Begin`/`End` (open-ended) or `Add`/`Shorten` (known expected end): `StateNode_Legacy` and
`StateNode_Reaction` add every executed action with the strategy that pushed it, lasting until the bot is free again
(cast time, reaction delay) or the next action starts.

## Legacy strategy list (`Legacy_*.json`)

JSON (`.json`) or XML (`.xml`, same structure, fields as attributes, root element `<brain>`).

```json
{
  "brain": "Legacy_Combat_Mage",
  "state": "Combat",
  "strategies": [
    {"add": "frost", "if": {"spec": 2}},
    {"add": ["dps assist", "ranged"]},
    {"add": "kite", "enabled": false},
    {"include": "Legacy_Combat_Common.json"}
  ]
}
```

The shipped files reproduce the AiFactory defaults:

- `Legacy_<State>_Common.json`: class-independent strategies (random/free bots, groups, battlegrounds, the
  `AiPlayerbot.*Strategies` config overrides), included by the files below.
- `Legacy_<State>.json`: every class's block, each under a `class` condition (used by the Default trees).
- `Legacy_<State>_<Class>.json`: one class's block, then the Common include.

Entries run in order. Each has exactly one of:

| field        | effect |
|--------------|--------|
| `add`        | add strategies (string, `"a, b"` or array) |
| `remove`     | remove strategies |
| `change`     | engine strategy string such as `"+a,-b"`, or `"config:<name>"` for a config list: `combatStrategies`, `nonCombatStrategies`, `reactStrategies`, `deadStrategies`, `randomBotCombatStrategies`, `randomBotNonCombatStrategies`, `randomBotReactStrategies`, `randomBotDeadStrategies` |
| `strategies` | a nested list (group) |
| `include`    | the strategy list of another Legacy file (relative to this file) |

Optional: `if` (condition), `else` (list run when `if` fails), `enabled: false` (entry is skipped),
`description` (ignored).

Top-level `"passive": ["follow", "stay"]` (also read from included files): those strategies stay in the engine, so
commands like `follow` / `stay` still set them and trees can test them (`has strategy::follow`), but they add no
triggers, multipliers or actions. The shipped NonCombat lists make `follow` and `stay` passive (Task_Follow moves the
bot) and the Reaction lists `follow` (its `stop follow` reaction). The debug addon marks them `(passive)`.

Strategies such as `aoe frost pve` are not listed: the placeholder strategies (`aoe`, `buff`, `cure`, ...) pick
them from the spec strategy and the content at runtime.

Conditions: all keys of an `if` object must match. `any: [ {...}, {...} ]` matches if one of them does,
`not: {...}` negates.

| key                 | value | true when |
|---------------------|-------|-----------|
| `class`             | class name(s) | bot class |
| `spec`              | 0, 1, 2 (or list) | talent tab with most points (below level 10: class default) |
| `minLevel` / `maxLevel` | number | bot level is in range |
| `battleground`      | bool | in a battleground |
| `bgType`            | AV, WS, AB, EY, IC | in that battleground |
| `arena`             | bool | in an arena (TBC/WotLK) |
| `autonomous`        | bool | random/free bot or AI on a real player's character |
| `freeBot`           | bool | random bot or free alt bot |
| `inGroup`           | bool | in a group |
| `groupLeader`       | bool | leads its group |
| `realPlayerMaster`  | bool | master is a real player |
| `hasMaster`         | bool | has a master |
| `masterIsBot`       | bool | master is a bot |
| `hasStrategy`       | name(s) | engine already has one of them (at this point of the list) |
| `chance`            | 0..1, or `jumpNoCombatChance` / `jumpMeleeInCombatChance` | random roll |
| `config`            | `jumpWithPlayer`, `jumpInBg`, `jumpFollow`, `jumpChase`, `randomBotJoinLfg`, `randomBotJoinBG`, `autoDoQuests`, `llmChat` | all listed config flags are on |

Unknown keys, classes, battlegrounds or config names are load errors.
