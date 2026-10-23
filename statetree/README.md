# State trees

Each bot state (NonCombat, Combat, Reaction, Dead) runs a state tree built when the bot is loaded:
`STAI_<State>.xml` (default trees) and `<Class>/STAI_<State>_<Class>.xml` (class trees and their subtrees), nodes with
conditions and tasks. Every class uses the default Dead tree.

The state machine (`playerbot/StateTree/StateMachine`) starts in NonCombat and switches between the main states
NonCombat, Combat and Dead (`TransitionTo`, from `PlayerbotAI`: combat start / end, death, resurrection); leaving a
state finishes its tree, entering one starts it. Reaction is additive: its tree runs on every update next to the
active main state, for what the bot does independent of its movement and actions (talking, rolling for loot,
accepting a resurrection, ...).

The legacy strategy engine and its `Legacy_*.json` strategy lists are gone (see `deprecated/README.md`); until the
new AI covers a behaviour its trees end in an Idle node (`Task_Wait`, delay 0) that waits for a transition.

Configured in `aiplayerbot.conf` (paths relative to its directory):

```
AiPlayerbot.StateTree.<State>.Default = statetree/STAI_<State>.xml
AiPlayerbot.StateTree.<State>.<Class> = statetree/<Class>/STAI_<State>_<Class>.xml
```

`<Class>` is Warrior, Paladin, Hunter, Rogue, Priest, Shaman, Mage, Warlock, Druid (DeathKnight on WotLK).
The class entry wins over Default. An empty value, or a file that fails to load (the error is in the server log
at startup), gives the state a single Idle node (`Task_Wait`, delay 0). Files are read at
server start. Every `STAI_*.xml` of `statetree` and its subfolders is loaded, so subtrees need no config entry.

The StateTree debug addon shows each state's tree nodes, the active one highlighted.

## STAI tree (`STAI_*.xml`)

```xml
<Root>
  <Global>
    <Task class="MasterDistance_GlobalTask">
      <Range value="follow"/>
    </Task>
  </Global>
  <Node name="Follow player">
    <Conditions>
      <Condition binding="Root|MasterDistance_GlobalTask::masterFar"/>
    </Conditions>
    <Task class="Task_MakePath">
      <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
    </Task>
    <Task class="Task_MoveTo">
      <AcceptableRadius value="follow"/>
      <Binding local="path" external="Root|Follow player|Task_MakePath::path"/>
      <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
    </Task>
  </Node>
  <Node name="Idle">
    <Conditions/>
    <Task class="Task_Wait">
      <Delay value="0"/>
    </Task>
    <Transitions>
      <Transition type="OnDelegate" source="Root|MasterDistance_GlobalTask::onMasterFar" node="Root|Follow player"/>
    </Transitions>
  </Node>
</Root>
```

There is always a single active path from the tree to the running node. A node is selected only while none runs
(the tree started, the node finished, a transition went to `Root`): the first node from the top whose conditions all
pass is entered (`OnEnter`); conditions are only checked then. A task can finish in `OnEnter` already (it returns
Succeeded / Failed, e.g. `Task_RollForItem`, the trades); else a tickable task (`IsTickable`) is updated every bot
update until it returns Succeeded or Failed (`OnExit`, the next update selects again), or until an `OnDelegate`
transition of the node fires; a task that is not tickable keeps running until a transition leaves the node. A task that finishes in its update hands over in the same update: the next task of the node is entered and
updated, a finished node is followed by the selection right away (up to 8 hand-overs per update). Nothing else leaves
a running node: a node that never finishes (Idle, `Task_Wait` with delay 0) needs `OnDelegate` transitions, for
example to Follow. A node whose task failed is not selected for 3 seconds. Empty `<Conditions/>` always pass, so a
last unconditional node is the fallback.

Tasks can be switched off at runtime in the debug addon (the v / x button right of a node's task,
`STAI_TaskContext::bDisabled`, reset when the bot despawns): the tree skips a switched-off task (the next one of the
node runs, a running one is left on the next update) and does not select a node whose tasks are all switched off.
Global tasks (providers) cannot be switched off.

Conditions test task properties: `<Condition binding="Root|..."/>` (see below) passes while the property is set (a
container not empty, else its bool value), or while it is not set with `negate="true"`; with
`compare="equal|lesser|lesser or equal|greater|greater or equal" value="<number>"` it compares a number property.
`compare="equal"` with a value that is not a number compares a text property (`<Condition
binding="Root|SpecProvider_GlobalTask::specName" compare="equal" value="frost mage"/>`), other compares need numbers.
`key="<number>"` reads the entry of a map property instead (`CooldownProvider_GlobalTask::ready`): it passes while the
entry is set (non-zero), `compare` / `negate` work on the entry, a missing key is not set (compared as 0).
`<Conditions match="any">` passes when one condition passes (default `all`). The debug addon shows each condition
green (passed), red (failed) or gray (not checked in the last update). (The legacy trigger conditions,
`<Condition trigger="..."/>`, went with the strategy engine.)

`<Task class="..." show_log="false">` keeps a task's runs out of the History (the idle `Task_Wait` of the Reaction
trees, which only waits for transitions); default `true`. Tasks whose runs repeat right after each other share one
History entry counting the runs, unless the task gives each run its own (`STAI_Task::AreConsecutiveLogsStacked`:
`Task_CastSpell`, `Task_CastBuff`).

Tasks:

| class                | settings | does |
|----------------------|----------|------|
| `Task_MakePath`      | `<Binding local="location" .../>`, or a fixed `<Location map="..." x="..." y="..." z="..."/>` (map optional: the bot's map) | calculates the path from the bot to the location on the core `PathfindingWorker` threads (see Paths below); Running until it is there, Succeeded with property `path` set, Failed without a location, without a way there (no portal route, no navmesh way from the bot) or after 10s |
| `Task_MoveTo`        | `<AcceptableRadius value="follow"/>` (yards or a range name, default `follow`), `<Binding local="path" external="Root\|<node>\|Task_MakePath::path"/>`, optional `<Binding local="location" .../>` | walks the leg of the path on the bot's map; a leg ending in a portal walks into its area trigger and succeeds once the bot is on the other side; Succeeded inside the radius of the destination, when the bound `location` moved further than the radius from the destination (the node plans again while the bot keeps walking) or when a path cut short brought the bot closer; Failed without a path, when it ended without bringing the bot closer, or after the path's walking time plus 10s; `OnExit` stops its movement unless it finished to plan again |
| `MasterDistance_GlobalTask` | `<Range value="follow"/>` (yards or a range name, default `follow`) | global task: property `masterFar` (a living master on another map or instance is always far), property `location` (where the master is, for `Task_MakePath`), delegate `onMasterFar` fired every update while the master is beyond the range |
| `BuffProvider_GlobalTask` | `<Buff spellid="..." scope="self\|single\|group"/>` per spell (any rank; scope optional, read from the spell's targets) | global task, not ticked: watches the buffs of the bot and its group (TeamSubsystem, SpellAuraWorldSubsystem); property `needs` (the casts to do), delegate `onMissingBuff` fired when an evaluation finds any; spells applying the same aura are one buff (Arcane Intellect / Arcane Brilliance) |
| `DispelProvider_GlobalTask` | `<Dispel type="magic\|curse\|disease\|poison"/>` per watched type (one or more), `<Aura spellid="..."/>` per aura (any rank), optional | global task, not ticked: watches the bot and its group for harmful auras of the watched dispel types (only the listed auras when there are some); property `whoNeeds` (every member with one, the bot first), properties `magic`, `curse`, `disease`, `poison` (the same by type, for one dispel spell each), delegate `onNeedsHelp` fired when an evaluation finds any |
| `CurseProvider_GlobalTask` | `<Aura spellid="..."/>` per aura (any rank), optional | `DispelProvider_GlobalTask` for curses only (every dispellable curse when no aura is listed), for the trees that only decurse; same properties and delegate |
| `Task_CastSpell`     | `spellid="..."` on the `<Task>` (spell id, or a name: the bot's highest rank), `<Binding local="target" .../>` and / or `<Binding local="whoNeeds" .../>`, or `<Target value="self"/>` (the bot first); `<Target value="dead"/>`: only dead targets (resurrections); `<Fallback spellid="..."/>` per spell, cast instead while the bot knows none of the spells before it (Flash Heal, then Heal, then Lesser Heal); `<SkipAura spellid="..."/>` (targets with that aura, any rank and caster, are skipped: Weakened Soul) / `<SkipAura own="true"/>` (targets with the bot's own aura of the cast spell: Renew) | casts the spell on `target`, else the first `whoNeeds` unit it can reach; waits for the cast and the GCD; Succeeded also without a living target, Failed when the bot knows none of its spells, reaches no living target, or every target is skipped by `<SkipAura>` (so the node waits its retry delay instead of being selected again at once) |
| `HealthProvider_GlobalTask` / `ManaProvider_GlobalTask` | none | global tasks, ticked: properties `healthPct` / `manaPct` (always 100 without mana), delegates `onHealthChanged` / `onManaChanged` fired when the whole percentage changes (and on the first update), at most once per second |
| `InstanceProvider_GlobalTask` | none | global task: properties `mapId`, `instanceId`, `inDungeon`, delegate `onInstanceChanged` fired when the bot is added to another map or instance (InstanceSubsystem); a change while the tree does not run fires when it starts again. On maps with stages (see below) ticked every 500ms: properties `stage` / `masterStage` (the stage the bot / master stands in, -1 none), `masterStageCleared` (the master is in a cleared stage; false while it is in no stage, true while it is not on the bot's map), `pathBlocked` (the way to `stagePosition`, else to the master out of follow range, crosses a stage that is not cleared other than the bot's and the master's), `stagePosition` (the bot's cleared point in the master's stage once it is cleared, else its uncleared point; unset without one), `stagePositionFar`; delegates `onStageBlocked` (the bot can neither follow nor go to a point: `pathBlocked` turned true, or the master went into a stage that is not cleared without an uncleared point, or out of every stage), `onStageCleared` (the master's stage got cleared), `onStagePositionFar` (every check while away from `stagePosition` and not blocked) |
| `Task_MoveToPosition` | `<AcceptableRadius value="2"/>` (yards, default 2), `<Binding local="position" .../>` | moves to the bound position (again when it changes; `MovePointAsync`, the path comes from the `PathfindingWorker`); Succeeded inside the radius or without a position, Failed after 30s; `OnExit` stops its movement |
| `CCProvider_GlobalTask` | `<Aura spellid="..."/>` per aura, optional | global task, ticked (every 250ms): the first unit marked with one of the bot's `rti cc` icons (default moon) that is alive, hostile, in sight, not crowd-controlled yet (polymorph, frost, sap, ... or a listed aura) and not the group's kill target (the first living `rti` mark, default skull); property `whoNeeds` (that unit), delegate `onCCNeeded` (a new target, and every second while it still needs crowd control) |
| `TargetProvider_GlobalTask` | `<WithThreat value="false"/>`, `<ThreatAmount value="90"/>` (percent, default 90) | global task, ticked: the unit to attack, the first living, hostile, in-sight mark of the bot's `rti` icons, else the least-health non-player unit on the threat lists of the bot and its group (kept while it stays valid); crowd-controlled units and the bot's `rti cc` marks are skipped; property `target` (a guid), delegate `onTargetChanged`. With `WithThreat` true and a living tank in the group, `target` is empty (property `threatHigh`, `onTargetChanged` fires) while the bot's threat on the creature is at `ThreatAmount` or more of the highest other threat on it (also before anybody else has threat), and is set again once it drops below |
| `Task_GetInRange`    | `<Range value="spell"/>` (yards or a range name), `<Binding local="target" .../>`, optional `<Target value="dead"/>` (the target is a dead body) | chases the target into range and line of sight, then stops the bot; Succeeded there (or without a living, or dead, target), Failed after 10s |
| `Task_Wait`          | `<Delay value="0"/>` (ms, default 0) | waits: Succeeded once its updates add up to the delay; with 0 it never finishes, the node is left through its transitions (an idle node) |
| `Task_EatAndDrink`   | `<Health until="100"/>`, `<Mana until="100"/>` (percent, default 100) | one meal: eats (everyone) and drinks (mana users) in parallel (a food / drink item of the bags, or with the item cheat the spell of the conjured food / drink of the bot's level); a food / drink that ends before its target is followed by the next one; Succeeded when both targets are reached or nothing is left to eat or drink after the bot ate or drank, Failed when nothing could be started within 3s; `OnExit` stands the bot up |
| `UIInteractionProvider_GlobalTask` | none | global task, ticked: what a player answers in its UI. Loot rolls the bot is asked to vote on (core `TeamSubsystem::OnLootRollStarted`, fired by `GroupLootRoll` for each voter): property `rolls` (the rolls not voted on yet, oldest first; dropped once voted or after the 60s roll timeout), delegate `onRollRequested` fired for every new roll. Trades (the bot's `SMSG_TRADE_STATUS`, `PlayerbotAI::onTradeStatus`): property `tradeOpenRequested` (a player opened a trade, the bot's window is not open yet) with delegate `onTradeRequested`, property `tradeAcceptRequested` (the window is open, the other player accepted, the bot did not) with delegate `onTradeAccepted` |
| `Task_OpenTrade`     | none | opens the bot's trade window (faces the trader); cancels the trade unless the trader is a bot, the master or a group member (with the security level for everything); Failed when it cancelled |
| `Task_AcceptTrade`   | none | accepts the trade (the other player accepted first), with the same trader rule; Failed when it cancelled |
| `Task_RollForItem`   | `<Binding local="rolls" external="Root\|UIInteractionProvider_GlobalTask::rolls"/>` | votes on every bound roll: greed for now (pass when greed is not allowed); not tickable, Succeeded in `OnEnter` |
| `Task_CastBuff`      | `<Binding local="needs" external="Root\|BuffProvider_GlobalTask::needs"/>` | casts one of its `needs`: self spell, single target spell for a member missing it alone, group spell when 2+ members of a subgroup miss it; waits for the cast and the GCD; Succeeded also when nothing is needed, Failed when nothing can be cast |
| `Task_UseItem`       | `<Item id="..."/>` per item, in the order they are tried | uses the first listed item of the bags whose spell is off cooldown (and its level allows) on the bot; with the item cheat an item it does not have by casting its spell; Succeeded once the item's spell went off, Failed when nothing can be used or the spell did not go off within 3s |
| `Task_Shoot`         | `<Binding local="target" .../>` | shoots the target with the equipped wand (Shoot, auto-repeat) while the node runs; Succeeded without a living target or when the bound target changed, Failed without a wand, when the target cannot be shot or the shooting stopped for 2s and cannot be started again; `OnExit` stops the shooting |
| `InterruptProvider_GlobalTask` | `spellid="..."` on the `<Task>` (the bot's interrupt, id or name) | global task, ticked (every 100ms): the enemies on the threat lists of the bot and its group that cast or channel an interruptible spell and that the interrupt reaches now (known, off cooldown, range, sight, mana); property `whoNeeds` (healing casts first), delegate `onInterruptNeeded` fired when the first caster or its spell changes |
| `HazardProvider_GlobalTask` | `<Margin value="2"/>` (yards, default 2) | global task, ticked (every 250ms): the area effects on the ground (dynamic objects) of units hostile to the bot with a harmful effect; property `inHazard` (the bot stands in one), property `safePosition` (the closest spot around the bot, 16 directions up to 20 yards, outside every hazard by the margin, in sight and walkable; kept while it stays safe; unset outside hazards), delegate `onHazard` fired when the bot got into one and every second while it is still in one |
| `CooldownProvider_GlobalTask` | `<Spell id="..."/>` per spell (any rank) | global task, ticked (every 250ms): property `ready` (`std::map` spell id -> the bot knows the spell or another rank of it and its own and category cooldowns are over, the GCD not included), read with a keyed condition `<Condition binding="Root\|CooldownProvider_GlobalTask::ready" key="11958"/>`; delegate `onCooldownChanged` fired when an entry changed |
| `HealProvider_GlobalTask` | `<Critical value="20"/>`, `<Low value="50"/>`, `<Medium value="70"/>`, `<AlmostFull value="90"/>` (percent, the bands' upper bounds), `<TankRange value="30"/>` (yards) | global task, ticked (every 200ms): the bot and its group members on its map. Properties `critical` / `low` / `medium` / `almostFull` (the living members below each bound, lowest health first, a tank before others of the same health; a member is in every band above its own; duelling members left out), `healTarget` (the first of them) and `lowestPct`; delegate `onNeedsHeal` (a member got wounded below `AlmostFull`, or a band got more members). `aggroed` (living members that are no tank and are the victim of a hostile unit, the bot first), `selfAttacked` (a hostile unit attacks the bot), delegate `onAggro` (a new member in `aggroed`). `dead` (members that are dead, not released and not offered a resurrection yet), `resurrectTarget` (the first), delegate `onDead`. `mainTank` (the first living tank other than the bot), `tankOutOfRange` (beyond `TankRange` or out of sight), delegate `onTankOutOfRange` (it turned true). Tanks come from `STAI_TeamRoles` (see `TeamProvider_GlobalTask`) |
| `TeamProvider_GlobalTask` | none | global task, ticked: the spec and roles of the bot and its group members on its map (`STAI_TeamRoles`: `PlayerSpec`, read again on the core `TeamSubsystem`'s group events and `OnTalentsChanged` / `OnMemberTalentsChanged`, and every 2s for stances / Righteous Fury); properties `tanks`, `healers`, `damage` (guids in group order, the bot included), `mainTank` (the first living tank other than the bot), `specName` / `isTank` / `isHealer` (the bot's own); delegate `onRolesChanged` (a role, spec, member or the main tank changed) |
| `Task_AcceptResurrection` | none | waits, dead, until a resurrection is offered to the bot and accepts it (as the client's `CMSG_RESURRECT_RESPONSE` would); Running while nothing is offered, Succeeded once accepted or when the bot is alive |
| `SpecProvider_GlobalTask` | none | global task, ticked (every 2s, and on the next update after the core `TeamSubsystem`'s `OnTalentsChanged`: talents learned, removed or a premade spec applied): property `specName`, the bot's spec from its talent tab with the most points (`PlayerSpec`, a class default below level 10): "frost mage", "fire mage" (arcane counts as frost), "holy priest", "disc priest", "shadow priest", "warlock", "rogue", "hunter", "elem shaman", "enh shaman", "resto shaman", "prot druid" (balance and feral), "resto druid", "holy paladin", "prot paladin", "retri paladin", "arms warrior", "fury warrior", "prot warrior"; delegate `onSpecChanged` fired when it changed |

The `rti` / `rti cc` icons of `TargetProvider_GlobalTask` and `CCProvider_GlobalTask` are the bot's own
(`PlayerbotAI::GetTargetIcons`, `playerbot/PlayerbotTargetIcons.*`), set by whispering it (or in party chat, every bot):
`rti skull,cross` (kill order: the first living unit marked skull, else cross; up to 8 icons), `rti cc circle` (the
crowd control mark: one icon), `rti none` / `rti cc none` (no icons), `rti ?` (or `rti`: the bot whispers both lists and the
unit each marks now) and `rti cc ?` (only the crowd control list); the WoWAddonInspectBot addon sets them from its
icon slots (`BOT	rti <icons>` / `BOT	rticc <icons>`). Both providers choose again on their next update when the
icons changed (`PlayerbotTargetIcons::GetVersion`). Defaults skull / moon; kept per bot in
`ai_playerbot_db_store` as the legacy values (`rti>skull,cross`, `rti cc>circle`), so orders saved by the old engine
still load.

RTS control (`playerbot/PlayerbotRtsc.*`, from the master only): `rtsc` switches it on and teaches the master Aedm
(spell 30758), whose ground casts the bots see (`PlayerbotAI::HandleMasterIncomingPacket`, `CMSG_CAST_SPELL`). While no bot
of the master is selected a cast within 10 yards of a bot selects it; while some are, a cast moves the selected bots
there (and deselects them) without selecting the bots near the spot; `rtsc select` / `cancel` /
`toggle`, `rtsc move` (every cast moves the bot), `rtsc save <name>` / `save selected <name>` (the next cast is saved),
`rtsc save here <name>`, `rtsc unsave <name>`, `rtsc show [<name>]`, `rtsc go <name>`, `rtsc last`, `rtsc reset`
(off, forgets everything, removes the spell). The move is a `MovePointAsync`; outside stages Follow player takes the
bot back once the master is out of follow range, in a dungeon with stages the spot is also saved as the bot's point
for the stage the master stands in, cleared or uncleared by the stage's state, nothing while the master is in no stage
(Position Yourself keeps it there). Saved locations are in `ai_playerbot_db_store` as the legacy values
(`RTSC saved location::<name>>map|x|y|z|o`). `rtsc jump` and `rtsc file` are not back yet.

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
<Node name="Idle">
  <Conditions/>
  <Task class="Task_Wait"> <Delay value="0"/> </Task>
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
The class NonCombat trees follow the master through `MasterDistance_GlobalTask`: Follow player runs `Task_MakePath` to
its `location`, then `Task_MoveTo` along the path, also when the master entered or left a dungeon. Each `<Class>/STAI_NonCombat_<Class>.xml`
only picks a subtree by the bot's map (`InstanceProvider_GlobalTask`): `STAI_NonCombat_Dungeon_<Class>.xml` in dungeon
and raid instances (`inDungeon`, Scholomance included), `STAI_NonCombat_Outdoors_<Class>.xml` everywhere else (for
now the same nodes as the Dungeon trees). `STAI_NonCombat.xml` (Default) also follows and positions itself.
`onInstanceChanged` sends the class tree back to Root to pick again. Every tree ends with Idle (`Task_Wait`, delay
0), which leaves through its transitions. Mage decurses and buffs: Idle goes to Check Decurse when someone is cursed
(Remove Lesser Curse) and to Check Missing Buffs when a buff (Arcane Intellect / Arcane Brilliance, Mage Armor) is
missing; each casts once per visit and is selected again while its `whoNeeds` / `needs` is not empty.
The class Combat trees of Mage, Priest, Shaman, Druid, Paladin and Warrior only pick a subtree by
`SpecProvider_GlobalTask::specName`, `<Class>/STAI_Combat_<Spec>_<Class>.xml` (e.g. `Mage/STAI_Combat_Frost_Mage.xml`,
`Priest/STAI_Combat_Shadow_Priest.xml`; Holy and Disc Priest share `Priest/STAI_Combat_Healer_Priest.xml`), and select again on `onSpecChanged`; Warlock, Rogue and Hunter have one spec and
keep a single tree. Every class Combat tree and `STAI_Combat.xml` start with Escape Hazard (`Task_MoveToPosition` to
`HazardProvider_GlobalTask::safePosition` while `inHazard`), and each of their other nodes goes there on `onHazard`:
leaving a spec node finishes its subtree (whatever it was doing), entering it again starts the subtree again (the
bot's instance is kept, its providers are entered again). `Mage/STAI_Combat_Frost_Mage.xml` (one tree for every map,
PvE, no AoE yet) runs, in this order: Ice Block (self, below 20%
health, while `CooldownProvider_GlobalTask::ready` has it), Interrupt (Counterspell on `InterruptProvider_GlobalTask::whoNeeds`), CC Target (`Task_CastSpell` Polymorph
on `CCProvider_GlobalTask::whoNeeds`), Decurse (Remove Lesser Curse on `CurseProvider_GlobalTask::whoNeeds`), Mana
Gem (`Task_UseItem`, below 30% mana), Evocation (self, below 15%, while ready), Mana Potion (`Task_UseItem`, below 15%), Wand
(`Task_Shoot` below 10% mana, left once it is back at 20% or the target changed), Attack Target (`Task_GetInRange`,
then `Task_CastSpell` Frostbolt on `TargetProvider_GlobalTask::target`, one cast per visit; `WithThreat` holds the
target back at 90% of the tank's threat) and Idle. Attack Target, Wand, Evocation and Idle leave on
`onHealthChanged` (Ice Block, entered only below 20%), `onInterruptNeeded`, `onCCNeeded` (and the others as listed in
the file). `Mage/STAI_Combat_Fire_Mage.xml` is the same with Fireball instead of Frostbolt and without Ice Block. The
Warlock Combat tree only has the Mage's Wand node (after Escape Hazard; below 10% mana on `TargetProvider_GlobalTask::target`, until 20%)
above Idle, which goes there on `onTargetChanged` / `onManaChanged`.
`Priest/STAI_Combat_Healer_Priest.xml` (Holy and Disc) heals from `HealProvider_GlobalTask`, each heal node getting in
35 yards of `healTarget` first (`Task_GetInRange`), in this order: Shield Critical (Power Word: Shield on `critical`,
skipping Weakened Soul), Critical Heal (Flash Heal, falling back to Heal / Lesser Heal), Fade (self, while
`selfAttacked`, a `mainTank` lives and Fade is ready), Shield Aggroed (the shield on `aggroed`: members that are no tank
and took aggro, the bot first), Shield Low and Low Heal (Greater Heal, then Heal / Lesser Heal) on `low`, Dispel Magic
on `DispelProvider_GlobalTask::magic`, Cure Disease (Abolish Disease, falling back to Cure Disease, not while the bot's
Abolish Disease is on the target) on `disease`, Medium Heal (Heal / Lesser Heal) on `medium`, Renew on `almostFull`
(not while the bot's Renew is on them), Mana Potion (below 15%), Shackle (Shackle Undead on the `CCProvider_GlobalTask`
mark), Follow Tank (`Task_GetInRange` 25 yards of `mainTank` while `tankOutOfRange`), then with more than 70% mana one
cast per visit of Shadow Word: Pain / Holy Fire (not while the bot's own is on the target) / Smite on
`TargetProvider_GlobalTask::target` (`WithThreat`, 90%), else Wand on it (left on the heal, aggro, tank, dispel, crowd
control and target delegates, or above 70% mana), and Idle (`Task_Wait` 1s, so a Renew that ran out or a heal node's
retry delay are noticed, sooner on those delegates). The Resto Druid, Elem Shaman and Resto Shaman subtrees only have
the Mage's Mana Potion node (below 15% mana) above Idle, which goes there on `onManaChanged`;
`Priest/STAI_Combat_Shadow_Priest.xml` has Mana Potion, then Wand, then Idle (back to Root on `onTargetChanged` /
`onManaChanged`). The other spec subtrees and the Rogue and Hunter Combat trees are only Idle for now.
`STAI_Dead.xml` (every class) accepts a resurrection offered to the bot (Accept Resurrection, `Task_AcceptResurrection`)
above Idle.
Every Reaction tree (`STAI_Reaction.xml` and each class's) trades and rolls for loot: Idle goes to Open Trade on
`onTradeRequested` and to Accept Trade on `onTradeAccepted` (each also selected while `tradeOpenRequested` /
`tradeAcceptRequested`, then back to Root), so the bot opens the window when its master starts a trade and accepts
once the master put something in and accepted. Loot: `UIInteractionProvider_GlobalTask` collects the
group's loot rolls, Idle (`Task_Wait` 0, `show_log="false"`) goes to Roll For Item on `onRollRequested`, the node is
also selected while `rolls` is not empty, and `Task_RollForItem` votes greed on them, then back to Root.

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
into its boxes with `bCanBeEnteredInCombatIfNotCleared` (default false, so the master pulls the creatures out).
`PlayerbotAI::CanCastSpell` checks it for charge spells (charge, intercept, feral charge); `Task_GetInRange` fails on
such a target (`PlayerbotAI::IsStageEntryBlocked`).

Every NonCombat tree (Default, and each class's Dungeon and Outdoors subtrees) uses them; outside instances there are
no stages, so Follow player's stage conditions always pass and Position Yourself is never selected:

- Follow player also needs `masterStageCleared` (the master is in a cleared stage: not in one that is not cleared and
  not between stages), `pathBlocked` false (the bot's way to the master crosses no stage that is not cleared, tested
  along the path every 500ms) and no `stagePosition`; it leaves for Idle on `onStageBlocked`: the bots wait where they
  are while the master is between stages or in a room that is not cleared and has no uncleared point for them (combat
  still takes them in), and follow again once the master is in a cleared stage.
- Position Yourself (`Task_MoveToPosition` to `stagePosition`) while the master is in a stage with a point for the bot
  and the bot is away from it (`stagePositionFar`); the other nodes go there on `onStagePositionFar`. Each stage has
  two points: the cleared one (used once the stage is cleared) and the uncleared one (before); the way to the
  uncleared point may cross the master's stage. The point is the bot's own override for the dungeon and stage (table
  `ai_playerbot_dungeon_positions` of the characters database, column `cleared`,
  `sql/characters/ai_playerbot_dungeon_positions.sql`), else the stage file's `clearposition` / `unclearedposition`.
  Moving a bot with rtsc in a dungeon saves the spot as its override for the master's stage, the cleared or the
  uncleared point by the stage's state. The entrance (stage 0) is always cleared: the first time a bot arrives in a
  dungeon (or starts its tree in the entrance stage) without entrance points, its spot is saved as both.

Every NonCombat tree eats and drinks through Eat and Drink (above Idle, below the buff / decurse nodes): Idle goes
there when health changed below 80% or mana below 90%, the node is also selected while one of them is that low, and
`Task_EatAndDrink` restores both to 100%.
The Priest NonCombat trees (Dungeon and Outdoors, every spec) also, below Follow player / Position Yourself and above Eat
and Drink: Resurrect (`Task_GetInRange` 25 yards of `HealProvider_GlobalTask::resurrectTarget` with `<Target
value="dead"/>`, then Resurrection on `dead`), Heal Group (Heal / Lesser Heal on `medium`, below 70%), Dispel Magic and
Cure Disease (as in combat) and Check Missing Buffs (Power Word: Fortitude / Prayer of Fortitude, Divine Spirit / Prayer
of Spirit, Shadow Protection / Prayer of Shadow Protection, Inner Fire); Idle goes there on `onDead`, `onNeedsHeal`,
`onNeedsHelp` and `onMissingBuff`.

Paths: the core `PathfindingWorker` (`src/game/MotionGenerators/PathfindingWorker.*`) calculates paths on its own
threads (detour queries of their own, `MMapManager::GetThreadNavMeshQuery`, under the mmap lock while tiles load or
unload), so map updates do not wait for them; requests are polled. The maps are one graph: each map's navmesh,
joined by the teleport area triggers (`areatrigger_teleport`, dungeon portals) as navlinks from the trigger to its
target. A path to another map is a leg per map, each ending in the portal to the next (legs on maps without a loaded
navmesh are straight lines; the bot plans again once it is there). Bots enter area triggers like a client does:
`PlayerbotAI::UpdateAreaTriggers` sends `CMSG_AREATRIGGER` when a bot walks into a teleport trigger, and the core
checks the requirements and teleports it as it would a player. `MotionMaster::MovePointAsync` is `MovePoint` with
its path from the worker (the unit stands until it is there).

Activity history (History tab of the debug addon): every bot keeps the last minute of activity in its
`STAI_ActivityLog`, reachable by tasks through `STAI_TaskContext::activity`. Tasks log their own runs, the tree
does not: `STAI_Task::StartLog` (in `OnEnter`), `LogStatus` (with a status of the run) and `EndLog` (left while running,
in `OnExit`) make one entry per run, named by the task's `GetDebugInfo()` (static: what the task does, e.g.
`Cast {spell:116}`, the addon shows the spell's icon and name) with the task's name as its source, once the task is
still running after its first update or fails (a task that succeeds at once is not logged). The task decides how its
run went: `Task_CastSpell` ends its entry with whether the spell went off (instant and channeled ones when they start,
cast-time spells with their spell go, also when the node is left during the cast), `Task_CastBuff` left during the GCD
is successful once the buff is not being cast any more, the movement and eat tasks left while running count as failed.
Global tasks and subtrees log nothing by default (`show_log="true"` switches it on); tasks may also add entries of
their own (`STAI_ActivityLog::Add` / `Shorten`).
