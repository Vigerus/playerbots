# Leveling design (Mage first)

Draft for review. Goal: a level 1 mage (Rottenfrost) on his own levels up the way a player would: decide what to
do, do it, and keep its combat working with the spells it has at its level.

## 1. Three layers on our machinery

| Layer | Question | Changes every | Where it lives | How it decides |
|---|---|---|---|---|
| Hour (strategy) | What am I working towards? Level up, gear up, gold | Minutes to hours (level up, zone done, goal reached) | Top of the NonCombat tree: an `Autonomous` state whose children are goals | **Utility** (`TrySelectChildrenWithHighestUtility` + considerations) |
| Minute (activity) | How do I get there right now? Quest, grind, train, sell, change zone | Seconds to minutes (quest done, bags full, new spells) | One subtree per activity (`STAI_Questing.xml`, ...) | **Ordered states** (priority list), like the trees we have |
| Second (reaction) | What do I press now? | Every update | Combat tree and spec subtrees, Reaction tree | Ordered states and delegates (what exists) |

Sensing and deciding stay separate, as in the rest of the trees: **providers only publish facts** (counts,
distances, levels, locations) and **the tree decides** (conditions, considerations). A provider never says "go
questing". It says "3 quests can be picked up within 150 yards". This keeps the decision visible and tunable in the
editor and the debug views, and lets several trees reuse the same facts.

### Why utility on top and ordered states below

- At the top the options compete on "how good" rather than "allowed or not": questing, grinding and dungeons are all
  possible at level 15, and the best one depends on several soft factors (quests left here, zone level, group,
  gear). This is what utility scoring is for.
- Below, the rules are mostly strict priorities: hand in before picking up, loot before walking away, eat before
  pulling. A utility score for these would be harder to reason about and would gain nothing. An ordered list is a
  readable rule set.
- Utility at the top has a known pitfall: **dithering**. Two options with close scores flip back and forth every time
  the bot re-evaluates. Our tree already prevents most of it, because the selection runs only when the path completes
  or a transition goes to Root, not every update. A goal state is left only when its activity subtree finishes
  (nothing left to do) or on a few "re-plan" delegates (level up, zone change). Add a small inertia bonus for the
  current goal (`Consideration_ActiveState`, below) so a slightly better option does not cause a switch.
- Alternatives considered: GOAP/HTN planners (they chain actions by preconditions and effects) are a good fit for
  open-ended crafting/economy goals. They are overkill for leveling, which has few options and a fixed recipe.
  Revisit them for the max-level "gold / gear" goals.

## 2. Tree layout

```
STAI_NonCombat_Mage.xml                (picks by map / leader)
├─ Dungeon                             existing subtree
├─ In Group                           existing Outdoors subtree (Follow Leader) while LeaderProvider_GlobalTask::hasLeader
└─ Autonomous                          new: STAI_NonCombat_Autonomous_Mage.xml
   ├─ Upkeep              in order     Decurse, Conjure Water / Food, Armor + Arcane Intellect, Eat and Drink
   ├─ Loot                              lootable corpses / quest objects nearby (LootProvider)
   ├─ Maintenance         utility      Train (new spells), Sell and Repair (bags full, durability), only above a threshold
   └─ Goal                utility      Level Up (below max level) | Gear Up / Gold (later, at max level)
      └─ Level Up         utility
         ├─ Questing     STAI_Questing.xml   (class independent)
         ├─ Grinding     STAI_Grinding.xml   (class independent)
         └─ Dungeon      later (level >= 15 and a group)
```

Class-independent subtrees (`STAI_Questing.xml`, `STAI_Grinding.xml`, `STAI_Training.xml`, `STAI_Vendor.xml`) hold
the activity logic once for every class. The class tree adds only class-specific states (conjure, armor, pets).
Each activity subtree declares its own movement goals in its `<GlobalTasks>`. They exist only while that subtree runs,
so nothing has to tell the goals which activity is active.

Combat stays as it is: the activity starts a fight by casting at a pull target (`Task_CastSpell` on
`HuntProvider_GlobalTask::pullTarget`). The core puts the bot in combat, the HLS switches to the Combat tree, and its
`TargetProvider_GlobalTask` finds the mob on the threat list. When combat ends, NonCombat starts again and Loot is
selected first. NonCombat and Combat do not need to share any data.

### Questing subtree (minute layer)

```
STAI_Questing.xml
├─ Hand In          QuestProvider::readyToHandIn            goal "Quest Taker"  -> Task_TurnInQuest
├─ Pick Up          QuestProvider::availableNear, logFree   goal "Quest Giver"  -> Task_AcceptQuest
├─ Work Objective   QuestProvider::objective                goal "Objective Area"
│  ├─ Use Item      quest item usable on a target here      -> Task_UseQuestItem
│  ├─ Gather        quest game object here                  goal "Interact" -> Task_Interact
│  ├─ Pull          HuntProvider::questTarget               goal "Pull Range" -> Task_CastSpell (opener)
│  └─ Search        nothing here yet                        Task_Wait (the goal walks to the next spawn point)
├─ Abandon          grey / impossible quests in the log     -> Task_AbandonQuest
└─ (none)           Failed: Level Up selects again (Grinding or Change Zone wins)
```

`Change Zone` is a sibling of Questing under Level Up. It wins when `ZoneProvider::levelDelta` says the bot has
outlevelled the zone, or when Questing failed because nothing is left here.

## 3. Shared knowledge (singletons, built once at startup or lazily on a worker thread)

| Index | Content | Source | Reuses |
|---|---|---|---|
| `QuestIndex` | `entry -> questId -> role flags` (giver, taker, objective 1..4), plus spawn positions per entry | quest relations, `quest_template` `ReqCreatureOrGOId` / `ReqItemId` / `SrcItemId`, loot tables (drops > 5% or quest class items), vendors | `deprecated/.../QuestValues.cpp` `EntryQuestRelationMapValue`, `TravelMgr::LoadQuestTravelTable` |
| `QuestHubIndex` | quest givers grouped into hubs (map, area, centre, faction, quest level range) | `QuestIndex` givers clustered by distance | new |
| `TrainerIndex` | class trainers per class and faction, with positions | `creature_template` trainer flags / `trainer_class`, spawns | new |
| `VendorIndex` | vendors, repairers, food / drink sellers per faction with positions | npc flags, `npc_vendor` | new |
| area levels | average creature level per area | `TravelMgr::GetAreaLevel` (exists) | live code |

These are read-only after construction, so every bot's map thread can read them without locks. That is the reason
to build them up front instead of per bot.

## 4. Providers (data only)

| Provider | Properties | Delegates | Notes |
|---|---|---|---|
| `ProgressProvider_GlobalTask` | `level`, `maxLevel`, `atMaxLevel`, `xpPct`, `money`, `autonomous` | `onLevelUp`, `onMoneyChanged` | `autonomous`: no leader (solo, or the bot leads its group), or its leader told it `autonomous on` (whisper) |
| `ZoneProvider_GlobalTask` | `areaId`, `zoneId`, `areaLevel`, `levelDelta` (bot - area), `zoneFits`, `nextHub` (STAI_Location), `nextHubLevel` | `onZoneChanged` | from area levels and `QuestHubIndex` (nearest hub of the bot's faction whose level range fits, reachable on foot or by known map transfers) |
| `QuestProvider_GlobalTask` | `logFree`, `readyToHandIn` (quest -> taker location), `takerLocation`, `availableNear` (count), `giverLocation` (nearest giver with an available quest), `objectiveQuest` / `objectiveIndex` / `objectiveEntries` / `objectiveArea` (the chosen open objective), `questItemTargets`, `greyQuests` | `onQuestCompleted` (an objective or a quest got done), `onQuestLogChanged`, `onQuestAvailable` | the "need quest objective" logic from `QuestValues.cpp` against `QuestStatusData` counters; givers checked with the same dialog-status rules as the client markers (`DialogStatusValue::getDialogStatus`). Polls every 1s at first, later driven by a core quest event |
| `HuntProvider_GlobalTask` | `pullTarget`, `questTarget` (a pull target that is a quest objective), `huntArea` (where level-fitting mobs spawn), `mobDensity` | `onPullTarget` | living hostile units in sight, not tapped, level -5..+2 (+0 when solo below 50% mana/health), no elites unless `can fight elite`, **safe pull**: no other unpulled hostile within its aggro radius + 5 yards |
| `LootProvider_GlobalTask` | `lootTarget` (a corpse the bot may loot, skinning later), `objectTarget` (a quest game object the bot needs, `GameObject::ActivateToQuest`) | `onLootable` | rules from `LootObjectStack.cpp` |
| `InventoryProvider_GlobalTask` | `freeSlots`, `food`, `drink` (counts of usable ones), `junkValue`, `durabilityPct`, `bagsFull` | `onBagsFull`, `onLowSupplies` | |
| `TrainerProvider_GlobalTask` | `newSpells` (count the bot can learn now and afford), `newSpellsCost`, `trainerLocation` | `onNewSpells` | `ObjectMgr::GetNpcTrainerSpells` / `GetNpcTrainerTemplateSpells` and `Player::GetTrainerSpellState` (green); evaluated on level up and money change, not every tick |
| `VendorProvider_GlobalTask` | `vendorLocation`, `repairLocation`, `foodVendorLocation` | | nearest friendly ones from `VendorIndex` |
| `SpellbookProvider_GlobalTask` | `knows` (map spell id -> known rank, keyed like `CooldownProvider::ready`) | `onSpellLearned` | lets one tree gate states on known spells. `CooldownProvider::ready` already means "known and off cooldown", so this is only for spells without cooldowns |

## 5. Tasks (do something, never move)

| Task | Does | Fails when |
|---|---|---|
| `Task_AcceptQuest` | at the giver: opens it and accepts every available quest the bot wants (`CMSG_QUESTGIVER_HELLO`, `CMSG_QUESTGIVER_ACCEPT_QUEST`), as the client would | out of reach, nothing accepted, log full |
| `Task_TurnInQuest` | at the taker: completes and chooses a reward (`CMSG_QUESTGIVER_COMPLETE_QUEST` / `CHOOSE_REWARD`); reward = an equipment upgrade by the spec's stat weights, else the highest sell price | out of reach, not complete |
| `Task_AbandonQuest` | drops the bound grey / impossible quests (`CMSG_QUESTLOG_REMOVE_QUEST`) | |
| `Task_Loot` | loots the bound corpse or object: takes quest items, then the rest while bags allow; releases the loot | out of reach, nothing to loot |
| `Task_Interact` | uses a quest game object (gather, click, open) | out of reach, it gave nothing |
| `Task_UseQuestItem` | uses a quest item on the bound target (rules from `CanUseItemOn`) | item missing, spell failed |
| `Task_LearnSpells` | at the trainer: buys every green spell it can afford, cheapest first (`CMSG_TRAINER_BUY_SPELL`) | out of reach, nothing learned |
| `Task_SellJunk` | at a vendor: sells grey items, then items with no use (simple rules first; `ItemUsageValue` ideas later) | out of reach |
| `Task_Repair` | repairs everything at a repairer | out of reach, no money |
| `Task_BuySupplies` | buys food / drink for its level (non-mages; mages conjure) | out of reach, no money |

The opener needs no new task: `Task_CastSpell` with `<Binding local="target" external="Root|HuntProvider_GlobalTask::pullTarget"/>`
and the spell's `<Fallback>` chain. Pulling is the minute layer's decision (which mob, when, safely). Fighting is the
second layer's.

## 6. Movement goals (in the activity subtrees)

| Goal | Kind | Spot | Priority (out of combat) |
|---|---|---|---|
| Quest Taker | location | `QuestProvider::takerLocation` | 30 |
| Quest Giver | location | `QuestProvider::giverLocation` | 28 |
| Objective Area | location | `QuestProvider::objectiveArea` (the next spawn point of an objective entry, cycled while nothing is found) | 20 |
| Pull Range | range (spell) | `HuntProvider::pullTarget` / `questTarget` | 32 |
| Loot | range (interact) | `LootProvider::lootTarget` / `objectTarget` | 34 |
| Trainer / Vendor | location | `TrainerProvider::trainerLocation`, `VendorProvider::*Location` | 30 |
| Change Zone | location | `ZoneProvider::nextHub` | 15 |
| Hunt Area | location | `HuntProvider::huntArea` | 12 |

Follow Leader (10) is not declared in the Autonomous subtree, so an autonomous bot never follows anybody.

## 7. New conditions and considerations

- `Consideration_ActiveState` (inertia): a score boost (for example 1.2) while the state is on the active path, so the
  current goal keeps winning against near-equal options.
- `Consideration_Bool`: 1 / 0 (or two values) from a bool property; avoids a curve for on/off facts.
- `Condition_Compare` with `key` already covers "knows spell X" (`SpellbookProvider::knows`) and cooldowns.

Level Up scores (starting values, tuned in game):

| Option | Considerations |
|---|---|
| Questing | `QuestProvider::availableNear + readyToHandIn + open objectives` (0..6 → 0..1), `ZoneProvider::levelDelta` (curve: 1 at -2..+3, falls to 0 at +6), inertia |
| Grinding | constant 0.3, `HuntProvider::mobDensity` (0..1), inertia |
| Change Zone | `levelDelta` (0 below +4, 1 at +6), or 0.5 when Questing failed in the last minute |
| Dungeon | 0 below level 15 or without a group (later) |

Maintenance: Train = `TrainerProvider::newSpells > 0` × distance curve (close trainer: do it now; far: wait for 2+
spells or a zone change); Sell and Repair = `bagsFull` or `durabilityPct < 30`.

## 8. Low level combat: one tree per spec, gated by known spells

The combat trees were written for level 60, so a level 1 bot knows almost none of their spells. Frostbolt is learned at 4.
The spec of a bot below level 10 is the class default (frost), so today a level 1 mage in Combat runs Attack Target
with Frostbolt: it fails, waits 3s, fails again, and the mage never casts.

Option A, one tree per spec with gates (recommended):
- Every spell state already fails harmlessly when the spell is unknown (`Task_CastSpell` fails, retry delay). Make it
  explicit and cheap: gate states on `CooldownProvider::ready` / `SpellbookProvider::knows`. The debug views then show
  them as not enterable instead of failing every 3s.
- `<Fallback>` for the filler: Frostbolt → Fireball (frost); Fireball only (fire). A level 1 mage casts Fireball.
- Add solo-only states, gated on "no tank in the group" (`TeamProvider_GlobalTask::tanks` empty): Frost Nova + step
  back when a mob is in melee (a `position` goal away from the target), Fire Blast as a finisher below 20% target
  health, Arcane Missiles, wand at low mana, and no threat cap without a tank (already the case).
- Rotation changes stay in one place per spec. The tree is somewhat longer.

Option B, a separate leveling tree (`STAI_Combat_Leveling_Mage.xml`) selected below level X:
- Simple to read per tree, but every rotation fix has to be made twice, and "below level X" is wrong anyway: a level
  40 bot still lacks talents and spells that a 60 has. The question that matters is "what do I know", not "what level am I".

Spell ranks are already handled: `Task_CastSpell` with a spell name casts the highest known rank, and the buff
provider casts the known rank. Also needed: the armor buff as one choice (Mage Armor, else Ice Armor, else Frost
Armor). Today only Mage Armor (34) is listed, so the bot casts no armor before level 34.

## 9. Core and module changes

- `GlobalEventSubsystem`: `OnQuestStatusChanged(Player*, questId)` (accept, objective counter, complete, reward,
  abandon) and `OnLevelUp(Player*, level)`; providers subscribe instead of polling. `OnQuestRewarded` exists already.
- Bots talk to NPCs through the client packets (`CMSG_QUESTGIVER_*`, `CMSG_TRAINER_BUY_SPELL`, `CMSG_SELL_ITEM`,
  `CMSG_REPAIR_ITEM`, `CMSG_LOOT`), like `Task_AcceptInvite`. The core checks distance, money and conditions, so a
  bot can do nothing a player could not.
- Autonomy switch: a whisper command `autonomous on|off` stored per bot in `ai_playerbot_db_store`; the NonCombat
  class tree chooses `Autonomous` while it is on or the bot has no leader (`LeaderProvider_GlobalTask::hasLeader`, done: only grouped bots have one).
- Debug: show the utility scores of the Goal / Level Up children (STATETREE_TODO "per node custom debug data").
  Without it, tuning the top layer is guesswork. Keep the three debug views in sync.

## 10. Build order (each step playable with Rottenfrost)

1. **Fights at level 1** (done): Fireball fallback, known-spell gates, Frost Armor in the buffs. Check: he kills a mob that attacks him.
2. **Autonomy + grinding** (done for Mage; autonomy is `!LeaderProvider::hasLeader`, ProgressProvider waits for step 3): `ProgressProvider` (autonomous), `HuntProvider`, `LootProvider`, `Task_Loot`, the
   Autonomous subtree with Upkeep / Loot / Grinding only. Check: he levels on his own in Deathknell.
3. **Training** (done: TrainerProvider, Task_LearnSpells, the Train subtree; plus InventoryProvider and Conjure Water / Food for the Mage): `TrainerIndex`, `TrainerProvider`, `Task_LearnSpells`, Maintenance → Train. Check: at 4 he walks to
   the trainer and learns Frostbolt, and the frost tree starts casting it.
4. **Quest pick up and hand in** and 5. **Quest objectives**: one route-driven QuestProvider fed by leveling
   guides (see 11 below): accept / turn in first, then kill and loot objectives, then the guide's XP grind steps.
6. **Zones and vendors**: `QuestHubIndex`, `ZoneProvider`, Change Zone, `InventoryProvider`, Sell and Repair. Check:
   he moves from Deathknell to Brill and on to Silverpine at the right levels.
7. **Utility tuning and debug scores**, then Dungeon (with a group) and the max-level goals.

## 11. QuestProvider from leveling guides (Guidelime)

Source: Guidelime guide addons (`F:\downloads\guidelime_all`): Olo (Horde 1-60: Tirisfal, Silverpine, Barrens,
Thousand Needles, Desolace, ...), Hakurai (Alliance 1-60), Nelf 1-12, Troll Mage 1-12 (Durotar; its `[GA Alliance]` is
wrong). About 4700 quest steps. A guide is `Guidelime.registerGuide([[ ... ]])`, one step per line, codes in brackets:
`N` name, `NX` next guide, `GA` faction / races / classes, `A` step applies to, `QA` / `QT` / `QC` / `QS` accept / turn
in / complete / skip a quest, `G x,y Zone` a spot in zone map percent, `XP n[+-xp]` grind to a level, `T` train, `V`
vendor, `R` repair, `H` hearth, `S` set hearth, `F` fly, `P` get a flight point, `UI` use an item, `CI` collect an
item, `O` / `OC` optional / comment.

Why a guide and not only the quest index: the index says which quests exist and where; a guide is a human tested
route, in an order that wastes no walking, skips bad quests and says where to grind and when to train.

1. **Converter** (`tools/GuideImport`, Python, offline): parses the guides into steps; `G` spots become world
   coordinates through `WorldMapArea.dbc` (zone bounds: `x = locTop - y% * (locTop - locBottom)`, `y = locLeft - x% *
   (locLeft - locRight)`, z from the map at load); writes `ai_playerbot_guide` (name, next, faction / race / class mask,
   level range) and `ai_playerbot_guide_step` (guide, index, type, quest, objective, map / x / y, xp target, applies
   mask, optional) into the playerbot SQL. Lua never reaches the server. Check the guides' licences before committing
   the converted data to the public repo.
2. **Validation** (same tool, against the world DB): every quest exists, its giver / taker spawns near the guide's
   spot, objectives resolve through the quest index; a report of the mismatches (wrong tags, removed quests).
3. **GuideIndex** (read-only singleton): guides by faction / race / level; each bot's guide and step kept in
   `ai_playerbot_db_store`.
4. **QuestProvider_GlobalTask**: publishes the current step (`stepType`, `stepQuest`, `stepLocation`, `stepNpc` near
   the spot, `objectiveEntries`, `xpTarget`, `onStepChanged`, `onQuestCompleted`). **The world says when a step is
   done, not a step pointer**: accepted once the quest is in the log, turned in once rewarded, complete once its
   counters are full, XP once the level is reached; after a death, a restart or a relog it fast-forwards to the first
   step not done. Steps the bot cannot do (race, `CanTakeQuest`) are skipped, comment-only steps (unequip gear,
   suicide back) dropped. Without a guide for the bot's race and level: the quest index (`QuestValues.cpp`'s entry ->
   quest -> role map) as fallback.
5. **Questing subtree**: a state per step type: Hand In, Accept, Work Objective (`HuntProvider` limited to the
   objective's entries, the guide's spot as the hunt area), Grind to XP (the Grind subtree), Travel; Train / Vendor /
   Hearth / Fly with build steps 3 and 6.
6. **Milestones**: converter + validation report; GuideIndex + progress; accept / turn in (`Task_AcceptQuest`,
   `Task_TurnInQuest`); kill / loot objectives; XP steps; train / vendor / travel steps.

## Open questions

- Should autonomous bots group with each other (two questers share kill credit) or stay solo for now?
- Map transfers by boat / zeppelin are not in the path graph. Should zone changes stay on one continent until they are?
- Reward choice: is a simple stat weight per spec enough, or reuse the deprecated `ItemUsageValue` scoring?
