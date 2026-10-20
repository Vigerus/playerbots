# State trees

Each bot state (NonCombat, Combat, Reaction, Dead) runs a state tree built when the bot is loaded:

- `STAI_<State>.xml` (default trees) and `<Class>/STAI_<State>_<Class>.xml` (class trees and their subtrees): the
  tree. Nodes with conditions and a task. Every class uses the default Dead tree.
- `Legacy_<State>[_<Class>].json`: config of the `StateNode_Legacy` / `StateNode_Reaction` task, the strategy
  list of the legacy engine (replaces `AiFactory::AddDefault<State>Strategies`).

Configured in `aiplayerbot.conf` (paths relative to its directory):

```
AiPlayerbot.StateTree.<State>.Default = statetree/STAI_<State>.xml
AiPlayerbot.StateTree.<State>.<Class> = statetree/<Class>/STAI_<State>_<Class>.xml
```

`<Class>` is Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid (DeathKnight on WotLK).
The class entry wins over Default. An empty value, or a file that fails to load (the error is in the server log
at startup), gives the state a single legacy node with the built-in AiFactory strategies. Files are read at
server start. Every `STAI_*.xml` of `statetree` and its subfolders is loaded, so subtrees need no config entry.

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
  <Node name="UtilityAI">
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
fires. A task that finishes in its update hands over in the same update: the next task of the node is entered and
updated, a finished node is followed by the selection right away (up to 8 hand-overs per update). Nothing else leaves
a running node: a node that never finishes (UtilityAI, the legacy engine) needs `OnDelegate` transitions, for
example to Follow. A node whose task failed is not selected for 3 seconds. Empty `<Conditions/>` always pass, so a
last unconditional node is the fallback.

Conditions are the bot's triggers, by the same names (and `::` qualifiers) as in strategies and custom strategies,
for example `critical health`, `far from master` or `has aura::spellid::23341::greater or equal::5`. A condition
passes while its trigger is active, or while it is inactive with `negate="true"`. `<Condition binding="Root|..."/>`
tests a task property instead (see below): it passes while the property is set (a container not empty, else its
bool value); with `compare="equal|lesser|lesser or equal|greater|greater or equal" value="<number>"` it compares a
number property. `<Conditions match="any">` passes when one condition passes (default `all`). Triggers are looked up per bot
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
| `Task_CastSpell`     | `spellid="..."` on the `<Task>` (spell id, or a name: the bot's highest rank), `<Binding local="target" .../>` and / or `<Binding local="whoNeeds" .../>` | casts the spell on `target`, else the first `whoNeeds` unit it can reach; waits for the cast and the GCD; Succeeded also without a living target, Failed when the bot does not know the spell or reaches no living target |
| `HealthProvider_GlobalTask` / `ManaProvider_GlobalTask` | none | global tasks, ticked: properties `healthPct` / `manaPct` (always 100 without mana), delegates `onHealthChanged` / `onManaChanged` fired when the whole percentage changes (and on the first update), at most once per second |
| `InstanceProvider_GlobalTask` | none | global task: properties `mapId`, `instanceId`, `inDungeon`, delegate `onInstanceChanged` fired when the bot is added to another map or instance (InstanceSubsystem); a change while the tree does not run fires when it starts again. On maps with stages (see below) ticked every 500ms: properties `stage` / `masterStage` (the stage the bot / master stands in, -1 none), `masterStageCleared` (the master is in no stage or a cleared one), `pathBlocked` (the way to `stagePosition`, else to the master out of follow range, crosses a stage that is not cleared other than the bot's), `stagePosition` (the bot's spot in the master's cleared stage, unset without one), `stagePositionFar`; delegates `onStageBlocked` (`masterStageCleared` turned false or `pathBlocked` true), `onStageCleared` (the master's stage got cleared), `onStagePositionFar` (every check while away from `stagePosition` and not blocked) |
| `Task_MoveToPosition` | `<AcceptableRadius value="2"/>` (yards, default 2), `<Binding local="position" .../>` | moves to the bound position (again when it changes); Succeeded inside the radius or without a position, Failed after 30s; `OnExit` stops its movement |
| `CCProvider_GlobalTask` | `<Aura spellid="..."/>` per aura, optional | global task, ticked (every 250ms): the first unit marked with one of the bot's `rti cc` icons (default moon) that is alive, hostile, in sight, not crowd-controlled yet (polymorph, frost, sap, ... or a listed aura) and not the group's kill target (the first living `rti` mark, default skull); property `whoNeeds` (that unit), delegate `onCCNeeded` (a new target, and every second while it still needs crowd control) |
| `TargetProvider_GlobalTask` | none | global task, ticked: the unit to attack, the first living, hostile, in-sight mark of the bot's `rti` icons, else the least-health non-player of its `possible attack targets` (kept while it stays valid); crowd-controlled units and the bot's `rti cc` marks are skipped; property `target` (a guid), delegate `onTargetChanged`; also sets the legacy `current target` value |
| `Task_GetInRange`    | `<Range value="spell"/>` (yards or a range name), `<Binding local="target" .../>` | chases the target into range and line of sight, then stops the bot; Succeeded there (or without a living target), Failed after 10s |
| `Task_Wait`          | `<Delay value="0"/>` (ms, default 0) | waits: Succeeded once its updates add up to the delay; with 0 it never finishes, the node is left through its transitions (an idle node) |
| `Task_EatAndDrink`   | `<Health until="100"/>`, `<Mana until="100"/>` (percent, default 100) | one meal: eats (everyone) and drinks (mana users) in parallel with the legacy food / drink actions (bags or cheat, not their thresholds), each once; Succeeded when both targets are reached or the food / drink auras ended (the node's conditions decide on another meal), Failed when nothing could be started within 3s; `OnExit` stands the bot up |
| `Task_CastBuff`      | `<Binding local="needs" external="Root\|BuffProvider_GlobalTask::needs"/>` | casts one of its `needs`: self spell, single target spell for a member missing it alone, group spell when 2+ members of a subgroup miss it; waits for the cast and the GCD; Succeeded also when nothing is needed, Failed when nothing can be cast |

New tasks implement `STAI_Task` (`playerbot/StateTree/Tasks/`; global tasks that publish properties and delegates for
the tree, the providers, in `playerbot/StateTree/Providers/`) and are registered in `STAI_BuiltinTasks.cpp`.

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
<Node name="UtilityAI">
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
  update, if the node is running and the target can be entered (its conditions, a failed node's retry delay;
  otherwise the node keeps running). A delegate fired while no node with a transition on it runs is remembered until
  one does.
- A transition can have its own `<Conditions>`: it is only taken while they pass (an OnDelegate transition whose
  conditions fail uses the delegate up, it waits for the next one).
- `node="Root"` selects again from the first node, `Root|<name>` enters that node (its conditions still have to pass).

Unknown paths, properties of different types and delegates that do not exist stop the server when the trees load.
The class NonCombat trees follow the master through `MasterDistance_GlobalTask`. Each `<Class>/STAI_NonCombat_<Class>.xml`
only picks a subtree by the bot's map (`InstanceProvider_GlobalTask`): `STAI_NonCombat_Dungeon_<Class>.xml` in dungeon
and raid instances (`inDungeon`), `STAI_NonCombat_Outdoors_<Class>.xml` everywhere else; Mage also has
`STAI_NonCombat_Scholomance_Mage.xml` (map 289, above Dungeon, for now the same as the Mage Dungeon tree).
`onInstanceChanged` sends the class tree back to Root to pick again. The legacy engine node of the trees is named
UtilityAI (task `StateNode_Legacy`). The Mage Outdoors subtree runs no legacy engine: its last node is Idle
(`Task_Wait`, delay 0); the Mage Dungeon / Scholomance subtrees end with UtilityAI (`Legacy_NonCombat_Mage.json`)
instead, with Idle's transitions. Mage decurses and buffs: Idle / UtilityAI go to Check Decurse when someone is cursed
(Remove Lesser Curse) and to Check Missing Buffs when a buff (Arcane Intellect / Arcane Brilliance, Mage Armor) is
missing; each casts once per visit and is selected again while its `whoNeeds` / `needs` is not empty.
`Mage/STAI_Combat_Mage.xml` (one tree for every map, no legacy engine) runs CC Target (`Task_CastSpell` Polymorph on
`CCProvider_GlobalTask::whoNeeds`), Attack Target (`Task_GetInRange`, then `Task_CastSpell` Frostbolt on
`TargetProvider_GlobalTask::target`, one cast per visit) and Idle (`Task_Wait` 0, left on `onCCNeeded` /
`onTargetChanged`). The `buff` strategy is not in the Legacy lists.

Stages: the rooms of a dungeon, registered once per map in the core `InstanceSubsystem` by the instance script
(Scholomance: `instance_scholomance.cpp`): boxes around the creature spawns of each room, its boss entries and the
positions bots stand at (per class mask / spec, best match wins). Stage 0 is the entrance and always cleared; another
stage is cleared once none of the creature spawns inside its boxes (no critters, triggers or creatures friendly to
players) is alive, and stays cleared for that instance. GM commands: `.instance stages` lists the stages of your map
with cleared / alive counts and the one you stand in, `.instance stages <n>` the creatures of stage n still alive,
`.instance stages show [n]` marks the edges of stage n's boxes (yours without n) with waypoint markers and its positions
with spotlights for a minute. The DungeonStage addon shows the stages of your instance (`BOT\tstages` addon request,
answered by the core `InstanceSubsystem` on the player's map thread, also without a group).

Bots do not go into a stage that is not cleared (other than the one they are in): out of combat never, in combat only
into its boxes with `bCanBeEnteredInCombatIfNotCleared` (default false, so the master pulls the creatures out). The
legacy engine checks it in `MovementAction::MoveTo` / `ChaseTo`, the reach actions (reach melee, reach spell, ...)
and `PlayerbotAI::CanCastSpell` for charge spells (charge, intercept, feral charge); `Task_GetInRange` fails on such a
target (`PlayerbotAI::IsStageEntryBlocked`).

The Dungeon (and Scholomance) subtrees use them:

- Follow player also needs `masterStageCleared` (the master is in no stage or a cleared one), `pathBlocked` false (the
  bot's way to the master crosses no stage that is not cleared, tested along the path every 500ms) and no
  `stagePosition`; it leaves for UtilityAI on `onStageBlocked`: the bots wait outside rooms the master pulls into (combat
  still takes them in) and follow again once the room is cleared.
- Position Yourself (`Task_MoveToPosition` to `stagePosition`) while the master is in a cleared stage with a position
  for the bot and the bot is away from it (`stagePositionFar`); the other nodes go there on `onStagePositionFar`.
  The position is the bot's own override for the stage (table `ai_playerbot_dungeon_positions` of the characters
  database, `sql/characters/ai_playerbot_dungeon_positions.sql`), else the stage's position for its class / spec.
  Moving a bot with rtsc in a dungeon saves the spot as its override for the stage containing it (else the master's
  stage).

Every NonCombat tree eats and drinks through Eat and Drink (above UtilityAI, below the buff / decurse nodes): UtilityAI goes
there when health changed below 80% or mana below 90%, the node is also selected while one of them is that low, and
`Task_EatAndDrink` restores both to 100%. The `food` strategy is not in the Legacy lists.

Activity history (History tab of the debug addon): every bot keeps the last minute of activity in its
`STAI_ActivityLog`, reachable by tasks through `STAI_TaskContext::activity`. The tree adds an entry per task (node name,
task name) from its `OnEnter` to its end, only for tasks still running after their first update: tasks that finish at
once are not logged, tasks that fail at once are (tasks return `false` from `LogsNodeActivity()` to opt out, like the
legacy nodes). A task sets `activitySpellId` / `activityResult` of its context to show a spell icon and Successful /
Fail with its entry (`Task_CastSpell`: instant and channeled spells succeed when they start, cast-time spells with
their spell go). Tasks add
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
- `Legacy_<State>_<Class>.json`: one class's block, then the Common include (no class Dead lists: every class
  uses `Legacy_Dead.json`).

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
