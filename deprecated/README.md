# Deprecated PlayerBots code

Code retired from the build while the bot AI is rebuilt on the StateTree (`playerbot/StateTree`, STAI trees in
`statetree/`). Nothing here is compiled: `CMakeLists.txt` only globs `playerbot/`, `playerbot/StateTree/**`,
`instancebot/` and `ahbot/`. Paths below mirror where the files used to be. When code is "removed" from PlayerBots it
is moved here, not deleted.

Code that stayed in `playerbot/` but lost its legacy parts is marked `TODO(strategy)` at the spot, saying what the
old code did there. `snapshots/playerbot/` has those files as they were before the cleanup (read-only reference,
the live versions are in `playerbot/`).

## What is here

| path | what it was |
|------|-------------|
| `playerbot/strategy/` | the ike3 decision engine: `Engine` (one per `BotState`), `Strategy` -> `Trigger` -> `Action` with relevance, `Multiplier`s, cached named `Value`s, all created by name in `*Context.h` (`NamedObjectContext`); `generic/` shared strategies, one folder per class |
| `playerbot/AiFactory.*` | builds the engines and the default strategy lists per class / spec / state (`AddDefault*Strategies`) |
| `playerbot/PlayerbotLegacyStrategies.*` | runtime strategy edits from the BotStateTree addon, per brain and map |
| `playerbot/StateTree/Tasks/StateNode_Legacy.*`, `LegacyBrain.*` | STAI task running a legacy engine as a tree node; the brains were the `statetree/Legacy_*.json` strategy lists (deleted) |
| `playerbot/StateTree/Tasks/StateTreeTask*.h`, `StateTree/StateNode.*` | first utility-AI prototype of a state node wrapping legacy actions |
| `playerbot/StateTree/Tasks/Task_Follow.*` | STAI follow task: kept a side of the master (relative angle, `GetNearPoint`) and repathed synchronously when the master moved; replaced by `Task_MakePath` + `Task_MoveTo` (async paths across maps) |
| `playerbot/StateTree/Tasks/Task_MakePath.*`, `Task_MoveTo.*`, `Task_MoveToPosition.*`, `Task_GetInRange.*`, `Task_MeleeAttack.*`, `StateTree/Providers/MeleePositioner_GlobalTask.*` | the movement of the nodes (a path to a location and walking it, a point move, chasing into range, melee positioning, the melee movement of the Reaction tree); replaced by the movement goals (`MovementGoal_GlobalTask`) driven by `playerbot/PlayerbotMovement.*`, which reuses their walking, chasing and behind logic |
| `playerbot/TravelMgr.*` | quest / rpg / grind / boss / gather / explore travel destinations, the bots' travel targets, fishing spots; the geography part (map transfers, area levels, bad mmaps, mob avoid areas) was kept in `playerbot/TravelMgr.*` |
| `playerbot/PlayerbotHelpMgr.*` | generated in-game help of strategies / triggers / actions / values |
| `playerbot/BotTests.*` | log based self tests of legacy behaviour |
| `playerbot/FleeManager.*` | picks a flee / safe spot away from enemies |
| `playerbot/LootObjectStack.*` | loot candidates of a bot and whether it can loot them |
| `playerbot/PlayerbotDbStore.*` | saved the bots' legacy values to `ai_playerbot_db_store` |
| `playerbot/BotState.h` | the legacy engine states (NonCombat, Combat, Dead, Reaction), replaced by `HLSStateType` |

Rescued out of `strategy/` (now live code): `playerbot/ItemQualifier.*` (`ItemQualifier`, `ItemUsage`),
`playerbot/ItemVisitors.h` (without the two "item usage" visitors), `playerbot/IterateItemsMask.h`,
`playerbot/Qualified.h`, `playerbot/PlayerSpec.*` (talent tab / roles, from `AiFactory`), `playerbot/LootAccess.h`
(loot template / vendor map access, from the loot and vendor values), `StateTree/Providers/STAI_TargetIcons.h`
(raid icon index, crowd control checks), and in `PlayerbotAI`: `GetSpellIdByName` (the "spell id" value),
`GetItemForSpell` (the "item for spell" value), `UpdateUpkeep` (master / group / afk upkeep of `DoNextAction`).

## Ideas worth keeping

Look here before writing the STAI replacement of a behaviour; most of these solved real problems.

- **Packet reactions** (`PlayerbotAI` constructor in `snapshots/`, `strategy/actions/*Action*`, `ExternalEventHelper`):
  opcode -> action tables for the bot's outgoing packets (group / guild / duel / arena invites, resurrect and summon
  requests, ready checks, loot rolls `SMSG_LOOT_START_ROLL`, trade status, quest updates, level up, petitions, LFG)
  and the master's packets (use game object, loot roll, quest accept / complete / share, taxi, release spirit,
  reclaim corpse). Some were delayed until the packet could be handled. This is the Reaction state's job now.
- **Chat**: command parsing and the command list (`ChatCommandHandlerStrategy`, `actions/*Action` per command), chat
  filters for addressing groups of bots (`@nc=`, `@co=`, `@star`, `@needrepair`, `@use`, ... in
  `snapshots/playerbot/ChatFilter.cpp`), chat replies (`ChatReplyAction`, reply chances per channel, mentions,
  toxic links / thunderfury replies in `snapshots/playerbot/PlayerbotAI.cpp`), LLM chat context per channel.
- **Class rotations**: `strategy/<class>/` strategies and triggers hold the spell priorities, buffs, cooldown use,
  interrupts and AoE thresholds per spec; `AiFactory::AddDefault*Strategies` shows which ones a spec ran.
- **Item usage** (`values/ItemUsageValue.*`): why an item is useful (equip upgrade, quest, skill, use, disenchant, AH,
  vendor, ammo, force need / greed), crafting reagent tables, bag space handling. Drives looting, selling, rolling.
- **Budget** (`values/BudgetValues.*`): money needed / free per purpose (repair, ammo, spells, travel, consumables,
  gear, guild, trade skill, AH, mount).
- **Travel** (`playerbot/TravelMgr.*`, `values/TravelValues.*`, `actions/ChooseTravelTargetAction.*`): travel
  destinations by purpose with partitions by distance, `PlayerTravelInfo` snapshot for worker threads, travel target
  states (travel / work / cooldown, retries, group copies), zone means for the node graph
  (`TravelNodeMap::generateZoneMeanNodes` lost its explore points), quest objective / giver locations from the DB.
- **RPG** (`actions/Rpg*`): how bots pick an NPC / object and interact (vendor, trainer, repair, quest, taxi, inn,
  AH, mail, emote) to look alive.
- **Targeting** (`values/*Target*Value.*`, `PossibleAttackTargetsValue`, `AttackersValue`): attacker lists, pull
  targets, RTI kill / CC order, enemy players, CC breakable checks (part rescued in `STAI_TargetIcons.h`).
- **Movement**: `JumpAction::CalculateJumpParameters` (knockback / jump landing physics, used for
  `SMSG_MOVE_KNOCK_BACK`), `FleeManager`, formations (`values/Formations.*`), positions / stay / guard
  (`values/PositionValue.*`), follow distances per range (`values/RangeValues.*`).
- **Loot** (`LootObjectStack.*`, `values/LootValues.*`, `actions/LootAction.*`): lootable / skinnable / gatherable
  checks, quest item filters, the skip list, roll decisions (`actions/LootRollAction.*`).
- **Persistence** (`PlayerbotDbStore.*`): values saved per bot and preset (outfits, rti, ranges, grouper / guilder
  overrides) in `ai_playerbot_db_store`.
- **Debugging**: strategy based debug output (`debug move`, `debug spell`, `debug loot`, `debug log`...), the
  remote "debug" commands (`state`, `target`, `values`, `travel`, `budget` in `snapshots/playerbot/PlayerbotAI.cpp`).

## PlayerbotPulls (`playerbot/PlayerbotPulls.*`)

The first dungeon plan: a per-master plan uploaded by the Bot Dungeon Planner addon and kept in the bot module
(`PlayerbotPullPlan`, `pull` / `plan` chat commands, record mode in an earlier version). Replaced by the core
`DungeonPlannerSubsystem` (presets in the database, shared by every character). Its pack generation moved there.
