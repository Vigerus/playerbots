# State trees

Each bot state (NonCombat, Combat, Reaction, Dead) runs a state tree built when the bot is loaded:
`STAI_<State>.xml` (default trees) and `<Class>/STAI_<State>_<Class>.xml` (class trees and their subtrees), nodes with
conditions and tasks. Every class but Shaman (`Shaman/STAI_Dead_Shaman.xml`, Reincarnation) uses the default Dead tree.

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
Each file is loaded once and shared by every bot (per bot only the task contexts differ); every `Task_RunSubtree`
gets its subtree linked at load, so a bot entering one only creates its contexts and loads nothing.

The StateTree debug addon (and the server's debug window) shows each state's tree nodes, the active one highlighted;
a `Task_RunSubtree` whose subtree does not run shows that subtree's nodes below the task, down to the leaves.

## STAI tree (`STAI_*.xml`)

```xml
<Root>
  <Global>
    <Task class="MasterDistance_GlobalTask">
      <Range value="follow"/>
    </Task>
    <Task class="BuffProvider_GlobalTask">
      <Buff spellid="1459"/>
    </Task>
    <Task class="MovementGoal_GlobalTask" name="Follow Master" priority="10" kind="location">
      <Radius value="follow"/>
      <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
      <Conditions>
        <Condition binding="Root|MasterDistance_GlobalTask::masterFar"/>
      </Conditions>
    </Task>
  </Global>
  <Node name="Check Buffs">
    <Conditions>
      <Condition binding="Root|BuffProvider_GlobalTask::needs"/>
    </Conditions>
    <Task class="Task_CastBuff">
      <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>
    </Task>
  </Node>
  <Node name="Idle">
    <Conditions/>
    <Task class="Task_Wait">
      <Delay value="0"/>
    </Task>
    <Transitions>
      <Transition type="OnDelegate" source="Root|BuffProvider_GlobalTask::onMissingBuff" node="Root|Check Buffs"/>
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
example to Check Buffs. The nodes decide what the bot does; where it goes is the movement goals' (see Movement below). A node whose task failed is not selected for 3 seconds. Empty `<Conditions/>` always pass, so a
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
`<Conditions match="any">` passes when one condition passes (default `all`). `<Any> <Condition .../> ... </Any>` in `<Conditions>` is
one condition that passes when one of its conditions passes (`IF (a OR b) AND c`: Eat and Drink). The debug addon shows each condition
green (passed), red (failed) or gray (not checked in the last update). (The legacy trigger conditions,
`<Condition trigger="..."/>`, went with the strategy engine.)

`<Task class="..." show_log="false">` keeps a task's runs out of the History (the idle `Task_Wait` of the Reaction
trees, which only waits for transitions); default `true`. Tasks whose runs repeat right after each other share one
History entry counting the runs, unless the task gives each run its own (`STAI_Task::AreConsecutiveLogsStacked`:
`Task_CastSpell`, `Task_CastBuff`).

Tasks:

| class                | settings | does |
|----------------------|----------|------|
| `MasterDistance_GlobalTask` | `<Range value="follow"/>` (yards or a range name, default `follow`) | global task: property `masterFar` (a living master on another map or instance is always far), property `location` (where the master is, for a `location` movement goal), delegate `onMasterFar` fired every update while the master is beyond the range |
| `BuffProvider_GlobalTask` | `<Buff spellid="..." scope="self\|single\|group"/>` per spell (any rank; scope optional, read from the spell's targets) | global task, not ticked: watches the buffs of the bot and its group (TeamSubsystem, SpellAuraWorldSubsystem); property `needs` (the casts to do), delegate `onMissingBuff` fired when an evaluation finds any; spells applying the same aura are one buff (Arcane Intellect / Arcane Brilliance) |
| `DispelProvider_GlobalTask` | `<Dispel type="magic\|curse\|disease\|poison"/>` per watched type (one or more), `<Aura spellid="..."/>` per aura (any rank), optional | global task, not ticked: watches the bot and its group for harmful auras of the watched dispel types (only the listed auras when there are some); property `whoNeeds` (every member with one, the bot first), properties `magic`, `curse`, `disease`, `poison` (the same by type, for one dispel spell each), delegate `onNeedsHelp` fired when an evaluation finds any |
| `CurseProvider_GlobalTask` | `<Aura spellid="..."/>` per aura (any rank), optional | `DispelProvider_GlobalTask` for curses only (every dispellable curse when no aura is listed), for the trees that only decurse; same properties and delegate |
| `Task_CastSpell`     | `spellid="..."` on the `<Task>` (spell id, or a name: the bot's highest rank), `<Binding local="target" .../>` and / or `<Binding local="whoNeeds" .../>`, or `<Target value="self"/>` (the bot first); `<Target value="dead"/>`: only dead targets (resurrections; a corpse guid of `HealProvider_GlobalTask::dead` is cast at as the corpse, `PlayerbotAI::CastSpellOnCorpse`); `<Fallback spellid="..."/>` per spell, cast instead while the bot knows none of the spells before it (Flash Heal, then Heal, then Lesser Heal); `<SkipAura spellid="..."/>` (targets with that aura, any rank and caster, are skipped: Weakened Soul) / `<SkipAura own="true"/>` (targets with the bot's own aura of the cast spell: Renew) | casts the spell on `target`, else the first `whoNeeds` unit it can reach; waits for the cast and the GCD; Succeeded also without a living target, Failed when the bot knows none of its spells, reaches no living target, or every target is skipped by `<SkipAura>` (so the node waits its retry delay instead of being selected again at once) |
| `HealthProvider_GlobalTask` / `ManaProvider_GlobalTask` | none | global tasks, ticked: properties `healthPct` / `manaPct` (always 100 without mana), delegates `onHealthChanged` / `onManaChanged` fired when the whole percentage changes (and on the first update), at most once per second |
| `InstanceProvider_GlobalTask` | none | global task: properties `mapId`, `instanceId`, `inDungeon`, delegate `onInstanceChanged` fired when the bot is added to another map or instance (InstanceSubsystem); a change while the tree does not run fires when it starts again. On maps with stages (see below) ticked every 500ms: properties `stage` / `masterStage` (the stage the bot / master stands in, -1 none), `masterStageCleared` (the master is in a cleared stage; false while it is in no stage, true while it is not on the bot's map), `pathBlocked` (the way to `stagePosition`, else to the master out of follow range, crosses a stage that is not cleared other than the bot's and the master's), `stagePosition` (the bot's cleared point in the master's stage once it is cleared, else its uncleared point; unset without one; while the bot stands in a stage that is not cleared other than the master's, where it last stood outside such a stage: it leaves the way it came instead of crossing the stage), `stagePositionFar`, `stagePositionFrom` (where the point comes from: `<stage> cleared|uncleared, rtsc|entrance / rtsc|stage file`, `pull <n> <name> [cleared], rtsc|waiting|planned|path <i>/<n>`, or `<bot's stage> uncleared, way back`; the Position Yourself goal's reason), `preset` / `presetName` (the master's Bot Dungeon Planner preset, 0 none), `activePull` / `activePullName` (its active pull, 0 none; see Plans below), `blockedBy` (the creature that would aggro on the way); delegates `onStageBlocked` (the bot can neither follow nor go to a point: `pathBlocked` turned true, or the master went into a stage that is not cleared without an uncleared point, or out of every stage), `onStageCleared` (the master's stage got cleared), `onStagePositionFar` (every check while away from `stagePosition` and not blocked), `onPlanChanged` (the master's preset, its content or its active pull changed) |
| `CCProvider_GlobalTask` | `<Aura spellid="..."/>` per aura, optional | global task, ticked (every 250ms): the first unit marked with one of the bot's `rti cc` icons (default moon) that is alive, hostile, in sight, not crowd-controlled yet (polymorph, frost, sap, ... or a listed aura) and not the group's kill target (the first living `rti` mark, default skull); property `whoNeeds` (that unit), delegate `onCCNeeded` (a new target, and every second while it still needs crowd control) |
| `TargetProvider_GlobalTask` | `<WithThreat value="false"/>`, `<ThreatAmount value="90"/>` (percent, default 90), `<AssistTank value="false"/>` (without a mark: the unit a living tank of the group attacks, melee), `<TankRange value="0"/>` (yards, 0: off), `<Aura spellid="..."/>` per aura watched on the target (any rank, any caster), optional | global task, ticked: the unit to attack, the first living, hostile, in-sight mark of the bot's `rti` icons, else the least-health non-player unit on the threat lists of the bot and its group (kept while it stays valid); crowd-controlled units and the bot's `rti cc` marks are skipped; property `target` (a guid), delegate `onTargetChanged`. With `WithThreat` true and a living tank in the group, `target` is empty (property `threatHigh`, `onTargetChanged` fires) while the bot's threat on the creature is at `ThreatAmount` or more of the highest other threat on it (also before anybody else has threat), and is set again once it drops below. With `TankRange`, property `tankNear` is true once a living tank of the group (not the bot) has come within `TankRange` of the chosen unit and stays true while it is chosen (also true without a group or another tank; always true without `TankRange`), delegate `onTankNear` (it turned true): melee dps waits with its Melee goal while the tank pulls the unit to itself. Property `auraStacks` (per configured aura id, read by key: the highest stack on `target`, 0 without it), delegate `onAuraChanged`: an internal subscription (`STAI_TargetAuraWatch`) to the target's aura events of the core `SpellAuraWorldSubsystem`, moved along with the target |
| `Task_Wait`          | `<Delay value="0"/>` (ms, default 0) | waits: Succeeded once its updates add up to the delay; with 0 it never finishes, the node is left through its transitions (an idle node) |
| `Task_EatAndDrink`   | `<Health until="100"/>`, `<Mana until="100"/>` (percent, default 100) | one meal: eats (everyone) and drinks (mana users) in parallel (a food / drink item of the bags, or with the item cheat the spell of the conjured food / drink of the bot's level); a food / drink that ends before its target is followed by the next one; Succeeded when both targets are reached or nothing is left to eat or drink after the bot ate or drank, Failed when nothing could be started within 3s; `OnExit` stands the bot up |
| `UIInteractionProvider_GlobalTask` | none | global task, ticked: what a player answers in its UI. Loot rolls the bot is asked to vote on (core `TeamSubsystem::OnLootRollStarted`, fired by `GroupLootRoll` for each voter): property `rolls` (the rolls not voted on yet, oldest first; dropped once voted or after the 60s roll timeout), delegate `onRollRequested` fired for every new roll. Trades (the bot's `SMSG_TRADE_STATUS`, `PlayerbotAI::onTradeStatus`): property `tradeOpenRequested` (a player opened a trade, the bot's window is not open yet) with delegate `onTradeRequested`, property `tradeAcceptRequested` (the window is open, the other player accepted, the bot did not) with delegate `onTradeAccepted`. Death popups: property `resurrectOffered` (the dead bot is offered a resurrection, `Task_AcceptResurrection`) with delegate `onResurrectOffered`, property `canReclaimCorpse` (the client's "Resurrect" button at the body: a ghost within 39 yards of its corpse, the reclaim delay over; `Task_ReclaimCorpse`) with delegate `onReclaimCorpse` |
| `Task_OpenTrade`     | none | opens the bot's trade window (faces the trader); cancels the trade unless the trader is a bot, the master or a group member (with the security level for everything); Failed when it cancelled |
| `Task_AcceptTrade`   | none | accepts the trade (the other player accepted first), with the same trader rule; Failed when it cancelled |
| `Task_RollForItem`   | `<Binding local="rolls" external="Root\|UIInteractionProvider_GlobalTask::rolls"/>` | votes on every bound roll: greed for now (pass when greed is not allowed); not tickable, Succeeded in `OnEnter` |
| `Task_CastBuff`      | `<Binding local="needs" external="Root\|BuffProvider_GlobalTask::needs"/>` | casts one of its `needs`: self spell, single target spell for a member missing it alone, group spell when 2+ members of a subgroup miss it; waits for the cast and the GCD; Succeeded also when nothing is needed, Failed when nothing can be cast |
| `Task_UseItem`       | `<Item id="..."/>` per item, in the order they are tried | uses the first listed item of the bags whose spell is off cooldown (and its level allows) on the bot; with the item cheat an item it does not have by casting its spell; Succeeded once the item's spell went off, Failed when nothing can be used or the spell did not go off within 3s |
| `Task_Shoot`         | `<Binding local="target" .../>` | shoots the target with the equipped wand (Shoot, auto-repeat) while the node runs; Succeeded without a living target or when the bound target changed, Failed without a wand, when the target cannot be shot or the shooting stopped for 2s and cannot be started again; `OnExit` stops the shooting |
| `InterruptProvider_GlobalTask` | `spellid="..."` on the `<Task>` (the bot's interrupt, id or name) | global task, ticked (every 100ms): the enemies on the threat lists of the bot and its group that cast or channel an interruptible spell and that the interrupt reaches now (known, off cooldown, range, sight, mana); property `whoNeeds` (healing casts first), delegate `onInterruptNeeded` fired when the first caster or its spell changes |
| `HazardProvider_GlobalTask` | `<Margin value="2"/>` (yards, default 2) | global task, ticked (every 250ms): the area effects on the ground (dynamic objects) of units hostile to the bot with a harmful effect; property `inHazard` (the bot stands in one), property `safePosition` (the closest spot around the bot, 16 directions up to 20 yards, outside every hazard by the margin, in sight and walkable; kept while it stays safe; unset outside hazards), delegate `onHazard` fired when the bot got into one and every second while it is still in one |
| `CooldownProvider_GlobalTask` | `<Spell id="..."/>` per spell (any rank) | global task, ticked (every 250ms): property `ready` (`std::map` spell id -> the bot knows the spell or another rank of it and its own and category cooldowns are over, the GCD not included), read with a keyed condition `<Condition binding="Root\|CooldownProvider_GlobalTask::ready" key="11958"/>`; delegate `onCooldownChanged` fired when an entry changed |
| `HealProvider_GlobalTask` | `<Critical value="20"/>`, `<Low value="50"/>`, `<Medium value="70"/>`, `<AlmostFull value="90"/>` (percent, the bands' upper bounds), `<TankRange value="30"/>` (yards) | global task, ticked (every 200ms): the bot and its group members on its map. Properties `critical` / `low` / `medium` / `almostFull` (the living members below each bound, lowest health first, a tank before others of the same health; a member is in every band above its own; duelling members left out), `healTarget` (the first of them) and `lowestPct`; delegate `onNeedsHeal` (a member got wounded below `AlmostFull`, or a band got more members). `aggroed` (living members that are no tank and are the victim of a hostile unit, the bot first), `selfAttacked` (a hostile unit attacks the bot), delegate `onAggro` (a new member in `aggroed`). `dead` (members that are dead and not offered a resurrection yet: on the bot's map with the body not released, else released, a ghost anywhere, with the corpse on the bot's map, as the corpse's guid), `resurrectTarget` (the first), delegate `onDead`. `mainTank` (the first living tank other than the bot), `tankOutOfRange` (beyond `TankRange` or out of sight), delegate `onTankOutOfRange` (it turned true). Tanks come from `STAI_TeamRoles` (see `TeamProvider_GlobalTask`) |
| `TeamProvider_GlobalTask` | none | global task, ticked: the spec and roles of the bot and its group members on its map (`STAI_TeamRoles`: `PlayerSpec`, read again on the core `TeamSubsystem`'s group events and `OnTalentsChanged` / `OnMemberTalentsChanged`, and every 2s for stances / Righteous Fury); properties `tanks`, `healers`, `damage` (guids in group order, the bot included), `mainTank` (the first living tank other than the bot), `specName` / `isTank` / `isHealer` (the bot's own); delegate `onRolesChanged` (a role, spec, member or the main tank changed) |
| `TotemProvider_GlobalTask` | `<Range value="30"/>` (yards, default 30) | global task, ticked (every 250ms, at once after a selection): the shaman's totems (`playerbot/PlayerbotTotems.*`, table `ai_playerbot_totems`): a totem or none per school, from the set of the dungeon stage the master stands in (the bot's own stage without the master on its map) when it has one, else the outdoor set; selected in the WoWAddonInspectBot addon. Properties `fire`, `earth`, `water`, `air` (the spell cast for the school: the highest known rank of the selection, 0 none), `needs` (those whose totem is not up: none in the school, another totem there, dead, or farther than `Range`; school order), `mapId` / `stage` (the set in use, 0 / 0 outdoor); delegates `onTotemsChanged` (a school's spell changed: a selection, a stage with another set, a new rank; and on the first check), `onTotemMissing` (`needs` got a spell it did not have: never cast, expired, destroyed, left behind) |
| `Task_CastTotem`     | `<Binding local="needs" external="Root\|TotemProvider_GlobalTask::needs"/>` | casts the first of its `needs` the bot can cast now (at itself); waits for the cast and the GCD; Succeeded also when nothing is needed, Failed when nothing can be cast (mana, cooldown) |
| `Task_SelfResurrect` | `<Spell id="..."/>` per allowed self resurrection (any rank), optional: every one | comes back by itself, dead, as the client's "Use Reincarnation / Soulstone" button does (`CMSG_SELF_RES`): uses the self resurrection the core set when the bot died (`PLAYER_SELF_RES_SPELL`: Reincarnation while known, off cooldown and an Ankh in the bags; a Soulstone) when it is allowed, off cooldown and its reagents are in the bags; Succeeded once alive (or when the bot is alive), Failed without one or when it did not bring the bot back within 5s |
| `CorpseProvider_GlobalTask` | none | global task of the Dead trees, ticked (every 500ms): property `released` (the bot is a ghost), `corpseLocation` (its corpse, any map, for a `location` movement goal), `corpseFar` (released and the corpse on another map or beyond 34 yards); delegate `onCorpseFar` (it turned true) |
| `Task_ReleaseSpirit` | none | releases the spirit as the client's "Release Spirit" button does (`CMSG_REPOP_REQUEST`), the ghost appears at the graveyard; not tickable, Succeeded once a ghost, Failed while it cannot (alive, just dying) |
| `Task_ReclaimCorpse` | none | comes back at the body as the client's "Resurrect" button does (`CMSG_RECLAIM_CORPSE`); not tickable, Succeeded once alive, Failed while it cannot (distance, reclaim delay) |
| `MeleeProvider_GlobalTask` | `<Binding local="target" .../>` (the unit fought: `TargetProvider_GlobalTask::target`, `TankTargetProvider_GlobalTask::target`), `<ChargeRange min="8" max="25"/>`, `<PullRadius value="15"/>` (yards), `<FightTime value="2000"/>` (ms) | global task, ticked (every 100ms), listed after the provider of its target: properties `inMelee` (in melee reach), `behind` (outside the target's front arc; a target turned to the bot for less than 1s still counts), `targetAttacksBot`, `targetHealthPct`, `targetDistance`, `enemiesInMelee` (units fighting the group within 8 yards), `inCombat` (the bot), `attacking` (the bot's auto attack is on the target), `swingQueued` (Heroic Strike / Cleave waits for the swing), `canCharge` (Charge / Intercept pull nothing else: the target has fought the group for `FightTime` and does not attack the bot, it is within `ChargeRange` and in sight, no hostile creature that is not fighting yet within `PullRadius` of the target or of the bot, never into a stage that is not cleared, also not its boxes that may be entered in combat); delegates `onMeleeChanged`, `onCanCharge` |
| `WarriorProvider_GlobalTask` | none | global task, ticked (every 100ms): properties `rage`, `stance` ("battle", "defensive", "berserker"), `knowsDefensive` / `knowsBerserker` (the stance nodes switch only to a stance the bot has), `revenge` (the bot blocked, dodged or parried: the core's `AURA_STATE_DEFENSE`), `overpower` / `overpowerTarget` (a target dodged: the warrior's combo point on it); delegate `onReactive` (Revenge or Overpower became usable) |
| `TankTargetProvider_GlobalTask` | `<Aura spellid="..."/>` per aura watched on the target, optional | global task of tank trees, ticked (every 250ms): the unit the bot holds among the units fighting the group (players, crowd-controlled units and the bot's `rti cc` marks left out). Main tank (the first living tank of the group, `STAI_TeamRoles`): a unit attacking a member that is no tank (the healers' first, the closest), else the `rti` mark, else the current target, else the closest unit not attacking the bot yet; off tank: a loose unit that does not attack the main tank, else the current target unless it attacks the main tank, else a unit attacking the bot, else the main tank's unit. Properties `target`, `taunt` (target while it attacks a member that is no tank), `loose`, `mainTank`; delegates `onTargetChanged`, `onLoose` (a new loose unit); `auraStacks` / `onAuraChanged` as on `TargetProvider_GlobalTask` |
| `MovementGoal_GlobalTask` | `name="..."`, `priority="..."`, `kind="location\|position\|range"`, `interruptCast="false"` on the `<Task>`; `<Radius value="2"/>` (location / position, yards or a range name), `<Range max="melee\|<yards>\|<range name>" min="0"/>`, `<Side value="any\|behind\|front"/>`, `<Sight value="true"/>`, `<Dead value="false"/>` (range); `<Binding local="location\|position\|target" .../>`; `<Conditions>` | a movement goal (see Movement): ticked, submits the goal to the bot's movement every update while its conditions pass and its spot is set; properties `wanted`, `active` (it drives the bot), `inRange` (the bot is where the goal wants it, also while another goal drives), `activeGoal` / `activeSource` (the goal driving the bot, `<goal> (<reason>)`)\| <goal>`), `movement` ("moving", "reached", "blocked", "yielding", "stopped", "none"); delegates `onGoalChanged` (it started / stopped driving), `onArrived` (`inRange` turned true) |
| `Task_StartAttack` | `<Binding local="target" .../>` | puts the auto attack on the target without moving (the tree's Melee goal brings the bot into reach); not tickable, Succeeded once the attack is on it, Failed without a living hostile target |
| `Task_StopAttack` | none | stops the auto attack; the Melee goal goes with the target (held back by threat), the bot stays where it is; not tickable, Succeeded |
| `Task_Stance` | `<Stance value="battle\|defensive\|berserker"/>` per stance, the first one the bot knows | casts the stance; not tickable, Succeeded when the bot is in it, Failed when it knows none or the switch did not happen |
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
(spell 30758; a cast of it switches it on too, also after a server restart), whose ground casts the bots see (`PlayerbotAI::HandleMasterIncomingPacket`, `CMSG_CAST_SPELL`). While no bot
of the master is selected a cast within 10 yards of a bot selects it; while some are, a cast moves the selected bots
there (and deselects them) without selecting the bots near the spot; `rtsc select` / `cancel` /
`toggle`, `rtsc move` (every cast moves the bot), `rtsc save <name>` / `save selected <name>` (the next cast is saved),
`rtsc save here <name>`, `rtsc unsave <name>`, `rtsc show [<name>]`, `rtsc go <name>`, `rtsc last`, `rtsc reset`
(off, forgets everything, removes the spell). The move is a `MovePointAsync` (the bot's movement yields to it); outside
stages the Follow Master goal takes the bot back once the master is out of follow range, in a dungeon with stages the spot is also saved as the bot's point
for the stage the master stands in, cleared or uncleared by the stage's state, nothing while the master is in no stage
(the Position Yourself goal keeps it there). Saved locations are in `ai_playerbot_db_store` as the legacy values
(`RTSC saved location::<name>>map|x|y|z|o`). `rtsc jump` and `rtsc file` are not back yet.

New tasks implement `STAI_Task` (`playerbot/StateTree/Tasks/`; global tasks that publish properties and delegates for
the tree, the providers, in `playerbot/StateTree/Providers/`) and are registered in `STAI_BuiltinTasks.cpp`.

Global tasks (`<Global>` directly under `<Root>`) belong to no node: they are entered when the tree starts and exited
when it stops. Tasks expose properties and delegates of their context (`STAI_Task::GetProperties` / `GetDelegates`),
addressed by path: `Root|<TaskClass>::<name>` for a global task, `Root|<node name>|<TaskClass>::<name>` for a node's
task (the first task of that class); `Root|<TaskClass>[<task name>]::<name>` picks the one with `name="<task name>"` on
its `<Task>` (several movement goals of a tree: `Root|MovementGoal_GlobalTask[Heal Range]::inRange`). A global task
may have its own `<Conditions>` (like a node's): the tree checks them before each update of the task and sets its
context's `bActive` (`STAI_TaskContext`), which the task reads (MovementGoal_GlobalTask: the goal is wanted only while
they pass). Every task context knows its tree (`STAI_TaskContext::tree`, the file stem).

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

## Movement

Where a bot goes is decided apart from what it does: the trees declare movement goals (`MovementGoal_GlobalTask`, one
per goal in `<Global>`, each with a `name`, a `priority`, a kind, its spot bound from a provider and its own
`<Conditions>`), and the bot's movement (`playerbot/PlayerbotMovement.*`, one per bot, updated after the state machine)
drives the goal with the highest priority of the goals wanted in that update, across the trees that run (the main
state's tree, its subtrees, the Reaction tree). There is always a goal: a goal stays wanted once reached (the movement
is "reached"); a goal that is not wanted any more stays the one shown (reached, or "stopped" before it was) with its
source until another drives, the bot standing where it is (Stay, `PlayerbotMovement | no goal yet`, before the first).
A goal may bind a `reason` (text) shown after it in its source: Position Yourself binds
`InstanceProvider_GlobalTask::stagePositionFrom` (`<stage> cleared|uncleared, rtsc|entrance / rtsc|stage file`). In a
dungeon with stages an rtsc move only saves the bot's stage point (the goal walks it there, reason rtsc); elsewhere
the rtsc move is direct and the movement yields to it. It is the only code that moves the bot; nodes only check whether the
bot is where a goal wants it (`Root|MovementGoal_GlobalTask[<goal>]::inRange`) and react to it (`onArrived`,
`onGoalChanged`, `onMovementChanged`: what the movement does changed, e.g. eat only while it is "reached").

- `location`: a `STAI_Location` on any map, walked along a `PathfindingWorker` path (across dungeon portals, leg by
  leg), planned again when the location moves away by more than the radius (the bot walks on meanwhile), when the
  bot changed map or the path ended short; a failed path waits 3s. Follow Master, Corpse Run.
- `position`: a `STAI_Position` on the bot's map, a point move (`MovePointAsync`). Position Yourself (the stage
  point), Escape Hazard (`HazardProvider_GlobalTask::safePosition`, `interruptCast`).
- `range`: in range and sight of a unit (or a corpse, `Dead`): `melee` chases it (at its back with `Side` behind,
  walking around it after 1s in its front arc while it attacks somebody else; in front with front) and faces it,
  other ranges chase into range and sight and stop there (a corpse is walked to). Melee, Attack Range, Heal Range,
  Follow Tank, Resurrect Range.

The movement yields to movement it did not give (a charge, fear, the rtsc move) until it is over, gives no new
movement while the bot casts unless the goal has `interruptCast` (Escape Hazard), does not follow a unit into a dungeon
stage that is not cleared (except its boxes that may be entered in combat while the group fights the unit, and after a
unit engaged by the group: it fights the group, attacks a member, and no hostile creature that is not fighting yet is
within 15 yards of it) and stops
the movement it gave when no goal is wanted. Goal priorities used by the trees: in combat Escape Hazard 100, Heal Range 60, Position
Yourself 55 (healer) / 45 (casters: Frost / Fire Mage, Elem / Resto Shaman, Shadow Priest, Warlock), Melee / Follow Tank
50, Attack Range 40; out of combat Resurrect Range 40, Position Yourself 35, Heal Range 25, Follow Master 10; dead
Corpse Run 10. The stage point (Position Yourself: the rtsc spot, else the stage file's) beats following the tank and
the attack range, a healer leaves it only to reach a member to heal, melee stays on its target. The BotStateTree addon shows the goal that drives the bot, its source
(`<goal> (<reason>)`), what the movement does and how far the destination is (snapshot record `M`).
The class NonCombat trees follow the master with the Follow Master goal (`location` on `MasterDistance_GlobalTask::location`,
reached within follow range), also when the master entered or left a dungeon. Each `<Class>/STAI_NonCombat_<Class>.xml`
only picks a subtree by the bot's map (`InstanceProvider_GlobalTask`): `STAI_NonCombat_Dungeon_<Class>.xml` in dungeon
and raid instances (`inDungeon`, Scholomance included), `STAI_NonCombat_Outdoors_<Class>.xml` everywhere else (for
now the same nodes as the Dungeon trees). `STAI_NonCombat.xml` (Default) has the same goals.
`onInstanceChanged` sends the class tree back to Root to pick again. Every tree ends with Idle (`Task_Wait`, delay
0), which leaves through its transitions. Mage decurses and buffs: Idle goes to Check Decurse when someone is cursed
(Remove Lesser Curse) and to Check Missing Buffs when a buff (Arcane Intellect / Arcane Brilliance, Mage Armor) is
missing; each casts once per visit and is selected again while its `whoNeeds` / `needs` is not empty.
The class Combat trees of Mage, Priest, Shaman, Druid, Paladin and Warrior only pick a subtree by
`SpecProvider_GlobalTask::specName`, `<Class>/STAI_Combat_<Spec>_<Class>.xml` (e.g. `Mage/STAI_Combat_Frost_Mage.xml`,
`Priest/STAI_Combat_Shadow_Priest.xml`; Holy and Disc Priest share `Priest/STAI_Combat_Healer_Priest.xml`), and select again on `onSpecChanged`; Warlock, Rogue and Hunter have one spec and
keep a single tree. Every class Combat tree and `STAI_Combat.xml` have the Escape
Hazard goal (priority 100, `position` on `HazardProvider_GlobalTask::safePosition` while `inHazard`, also while casting):
the bot walks out whatever its spec subtree does, the rotation goes on. Leaving a spec node finishes its subtree,
entering it again starts the subtree again (the bot's instance is kept, its providers are entered again). `Mage/STAI_Combat_Frost_Mage.xml` (one tree for every map,
PvE, no AoE yet) runs, in this order: Ice Block (self, below 20%
health, while `CooldownProvider_GlobalTask::ready` has it), Interrupt (Counterspell on `InterruptProvider_GlobalTask::whoNeeds`), CC Target (`Task_CastSpell` Polymorph
on `CCProvider_GlobalTask::whoNeeds`), Decurse (Remove Lesser Curse on `CurseProvider_GlobalTask::whoNeeds`), Mana
Gem (`Task_UseItem`, below 30% mana), Evocation (self, below 15%, while ready), Mana Potion (`Task_UseItem`, below 15%), Wand
(`Task_Shoot` below 10% mana, left once it is back at 20% or the target changed), Attack Target (`Task_CastSpell`
Frostbolt on `TargetProvider_GlobalTask::target` once the Attack Range goal has the bot in spell range and sight, one
cast per visit; `WithThreat` holds the
target back at 90% of the tank's threat) and Idle. Attack Target, Wand, Evocation and Idle leave on
`onHealthChanged` (Ice Block, entered only below 20%), `onInterruptNeeded`, `onCCNeeded` (and the others as listed in
the file). `Mage/STAI_Combat_Fire_Mage.xml` is the same with Fireball instead of Frostbolt and without Ice Block. The
Warlock Combat tree only has the Mage's Wand node (with its Attack Range goal; below 10% mana on `TargetProvider_GlobalTask::target`, until 20%)
above Idle, which goes there on `onTargetChanged` / `onManaChanged`.
`Priest/STAI_Combat_Healer_Priest.xml` (Holy and Disc) heals from `HealProvider_GlobalTask`; its goals keep the bot in
35 yards of `healTarget` (Heal Range), else in 25 of `mainTank` while `tankOutOfRange` (Follow Tank), else in spell
range of the target (Attack Range), and each heal node waits for Heal Range's `inRange`, in this order: Shield Critical (Power Word: Shield on `critical`,
skipping Weakened Soul), Critical Heal (Flash Heal, falling back to Heal / Lesser Heal), Fade (self, while
`selfAttacked`, a `mainTank` lives and Fade is ready), Shield Aggroed (the shield on `aggroed`: members that are no tank
and took aggro, the bot first), Shield Low and Low Heal (Greater Heal, then Heal / Lesser Heal) on `low`, Dispel Magic
on `DispelProvider_GlobalTask::magic`, Cure Disease (Abolish Disease, falling back to Cure Disease, not while the bot's
Abolish Disease is on the target) on `disease`, Medium Heal (Heal / Lesser Heal) on `medium`, Renew on `almostFull`
(not while the bot's Renew is on them), Mana Potion (below 15%), Shackle (Shackle Undead on the `CCProvider_GlobalTask`
mark), then with more than 70% mana (and Attack Range in range) one
cast per visit of Shadow Word: Pain / Holy Fire (not while the bot's own is on the target) / Smite on
`TargetProvider_GlobalTask::target` (`WithThreat`, 90%), else Wand on it (left on the heal, aggro, tank, dispel, crowd
control and target delegates, or above 70% mana), and Idle (`Task_Wait` 1s, so a Renew that ran out or a heal node's
retry delay are noticed, sooner on those delegates). The Resto Druid subtree only has the Mage's Mana Potion node
(below 15% mana) above Idle, which goes there on `onManaChanged`. The three Shaman subtrees keep their totems
(`TotemProvider_GlobalTask`): `Shaman/STAI_Combat_Elem_Shaman.xml` runs Interrupt (Earth Shock on
`InterruptProvider_GlobalTask::whoNeeds`; Earth Shock is not cast otherwise), Mana Potion (below 15%), Totems
(`Task_CastTotem` on `needs`, where the bot stands), Attack Target (Lightning Bolt on `TargetProvider_GlobalTask::target`
once the Attack Range goal has the bot in range, `WithThreat` 90%, one cast per visit) and Idle (`Task_Wait` 1ms; to
Interrupt, Totems on `onTotemMissing`, Attack Target, Root on `onTotemsChanged` / low mana); Resto Shaman has Mana
Potion, Totems and Idle, Enh Shaman Totems and Idle (`Task_Wait` 1s, so a totem that failed is tried again);
`Priest/STAI_Combat_Shadow_Priest.xml` has Mana Potion, then Wand, then Idle (back to Root on `onTargetChanged` /
`onManaChanged`). The Warrior spec subtrees only do the rotation in melee reach (`MeleeProvider_GlobalTask::inMelee`,
rage and stance from `WarriorProvider_GlobalTask`); Start Attack (`Task_StartAttack`) puts the auto attack on the target
and the subtree's Melee goal (`range` melee) moves the bot to it: behind it for fury and arms, in front for prot. `Warrior/STAI_Combat_Fury_Warrior.xml` (berserker stance, battle below 30)
and `Warrior/STAI_Combat_Arms_Warrior.xml` (battle stance) take `TargetProvider_GlobalTask` with `WithThreat` 90 and
`AssistTank` (the tank's unit; nothing before the tank has threat: Hold Threat stops the auto attack, the Melee goal
goes with the target), `TankRange` 10 (the Melee goal, Intercept and Charge wait for `tankNear`: a tank that pulls with
a bow and waits out of sight is not run past) and the Sunder Armor aura, run Health Potion, the stance, Interrupt
(Pummel, fury), Bloodrage, Intercept (fury, berserker) / Charge (arms, the bot out of combat) only while `canCharge`,
Start Attack, Sunder Armor (below 5 stacks on the target, not below 20% health), Battle Shout, Overpower (arms), Execute
(below 20%), Bloodthirst / Whirlwind (fury) or Sweeping Strikes / Mortal Strike (arms), Cleave (2+ units in 8 yards) /
Heroic Strike (rage to spare, not while one is queued) and Idle (200ms; back to Root on `onTargetChanged` /
`onMeleeChanged`). `Warrior/STAI_Combat_Prot_Warrior.xml` holds `TankTargetProvider_GlobalTask::target`: Last Stand,
Shield Wall, Health Potion, Charge (battle stance, the bot out of combat, `canCharge`), Defensive Stance (battle below
10), Taunt (`taunt`, in 20 yards), Interrupt (Shield Bash), Bloodrage, Start Attack, Sunder Stacks (below 5 stacks, `<Aura>` on the provider), Revenge, Shield Block (the unit
hits the bot), Shield Slam, Demoralizing Shout (not on units with it), Battle Shout, Cleave, Sunder Armor (the threat
filler), Heroic Strike, Idle. The Warrior NonCombat subtrees put prot warriors into defensive stance (the group sees
them as tanks) and fury warriors into berserker stance between fights.
The other spec subtrees and the Rogue and Hunter Combat trees are only Idle for now.
`STAI_Dead.xml` (every class but Shaman) releases the spirit (Release Spirit, `Task_ReleaseSpirit`, while not
`CorpseProvider_GlobalTask::released`), the Corpse Run goal walks the ghost back to its body (`location` on
`corpseLocation`, reached within 10 yards; a body in a dungeon through its portal, where the core resurrects the ghost), Idle
waits (`Task_Wait` 1s); the Reaction tree accepts resurrections and reclaims the body.
`Shaman/STAI_Dead_Shaman.xml` first comes back by Reincarnation (`Task_SelfResurrect`, before releasing) when the
core offers it (off cooldown, an Ankh in the bags). The Shaman NonCombat subtrees
(Dungeon and Outdoors) resurrect dead members with Ancestral Spirit like the Priest's Resurrect node
(`HealProvider_GlobalTask::dead`, `onDead`).
Every Reaction tree (`STAI_Reaction.xml` and each class's) trades and rolls for loot: Idle goes to Open Trade on
`onTradeRequested` and to Accept Trade on `onTradeAccepted` (each also selected while `tradeOpenRequested` /
`tradeAcceptRequested`, then back to Root), so the bot opens the window when its master starts a trade and accepts
once the master put something in and accepted. Loot: `UIInteractionProvider_GlobalTask` collects the
group's loot rolls, Idle (`Task_Wait` 0, `show_log="false"`) goes to Roll For Item on `onRollRequested`, the node is
also selected while `rolls` is not empty, and `Task_RollForItem` votes greed on them, then back to Root. Death: Idle
goes to Accept Resurrection on `onResurrectOffered` (`Task_AcceptResurrection`, also for a resurrection cast at the
ghost's corpse) and to Reclaim Corpse on `onReclaimCorpse` (`Task_ReclaimCorpse`, the ghost at its body after the
Dead tree's corpse run), each also selected while `resurrectOffered` / `canReclaimCorpse`, then back to Root.

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
`PlayerbotAI::CanCastSpell` checks it for charge spells (charge, intercept, feral charge: never into such a stage); the
movement's `range` goals stop short of such a target (`PlayerbotAI::IsStageEntryBlocked`), unless the target is engaged:
it fights the group, attacks a member and no hostile creature that is not fighting yet stands within 15 yards of it
(the fight goes on inside the room, the bots follow it without pulling the next pack).

Every NonCombat tree (Default, and each class's Dungeon and Outdoors subtrees) uses them; outside instances there are
no stages, so Follow Master's stage conditions always pass and Position Yourself is never wanted:

- the Follow Master goal also needs `masterStageCleared` (the master is in a cleared stage: not in one that is not
  cleared and not between stages), `pathBlocked` false (the bot's way to the master crosses no stage that is not
  cleared, tested along the path every 500ms) and no `stagePosition`: the bots stop where they are while the master is
  between stages or in a room that is not cleared and has no uncleared point for them (combat still takes them in),
  and follow again once the master is in a cleared stage.
- the Position Yourself goal (`position` on `stagePosition`, priority above Follow Master) while the master is in a stage
  with a point for the bot and the way is not blocked (reached at the point). Each stage has
  two points: the cleared one (used once the stage is cleared) and the uncleared one (before); the way to the
  uncleared point may cross the master's stage. The point is the bot's own override for the dungeon and stage (table
  `ai_playerbot_dungeon_positions` of the characters database, column `cleared`,
  `sql/characters/ai_playerbot_dungeon_positions.sql`), else the stage file's `clearposition` / `unclearedposition`.
  Moving a bot with rtsc in a dungeon saves the spot as its override for the master's stage, the cleared or the
  uncleared point by the stage's state. The entrance (stage 0) is always cleared: the first time a bot arrives in a
  dungeon (or starts its tree in the entrance stage) without entrance points, its spot is saved as both.

Plans: Bot Dungeon Planner presets (core `DungeonPlannerSubsystem`, `src/game/Maps/DungeonPlannerSubsystem.*`, tables
`dungeon_planner_presets` / `dungeon_planner_pulls` / `dungeon_planner_selections` of the characters database, created
on first use). Every dungeon has generated packs (its hostile creature spawns grouped by spawn group, creature_linking
and distance): the planner addon's (`/bdp`) creature dots. A preset is an ordered list of pulls with a waiting
position, a cleared position and a path each, shared by every character; a player selects one per dungeon (or none).
With the master's preset selected the `InstanceProvider_GlobalTask` ignores the stages: `stagePosition` is the next
waypoint of the active pull's path (from the one nearest to the bot), then its waiting position; once its creatures
are dead its cleared position. The bot's rtsc spot for the pull and state (`ai_playerbot_dungeon_positions` with the
preset's id, removed with the preset) wins over the preset's position. Without a preset the stages work as before
(their rtsc spots have preset NULL). Next / Prev in the DungeonStage addon move the active pull; the subsystem's
OnPlanChanged (selection, save, delete, Next / Prev) reaches the provider, which updates at once and fires
`onPlanChanged`.
In every dungeon the way to `stagePosition` / the master is also blocked (`pathBlocked`, `blockedBy`) while it passes
within aggro range (+3 yards) of a living hostile creature of the packs that is not fighting yet (not one the bot is in
range of already, nor one in range of a chosen spot); the bot tells its master which creature, at most every 15s.

Every NonCombat tree eats and drinks through Eat and Drink (below 80% health or below 90% mana, an `<Any>`), above
Idle and below the buff / decurse nodes, only while the bot stands (the movement "reached";
`onMovementChanged` leaves the node): Idle goes there when health / mana changed that low, and `Task_EatAndDrink` restores
both to 100%.
The Priest NonCombat trees (Dungeon and Outdoors, every spec) also, above Eat and Drink: Resurrect (Resurrection on
`dead` once the Resurrect Range goal, 25 yards of `HealProvider_GlobalTask::resurrectTarget`, has the bot in range), Heal
Group (Heal / Lesser Heal on `medium`, below 70%, once the Heal Range goal has the bot in 35 yards of `healTarget`), Dispel Magic and
Cure Disease (as in combat) and Check Missing Buffs (Power Word: Fortitude / Prayer of Fortitude, Divine Spirit / Prayer
of Spirit, Shadow Protection / Prayer of Shadow Protection, Inner Fire); Idle goes there on `onDead`, `onNeedsHeal`,
`onNeedsHelp`, `onMissingBuff` and the goals' `onArrived`.

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
