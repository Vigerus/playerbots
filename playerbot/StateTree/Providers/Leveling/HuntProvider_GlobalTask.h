#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <memory>
#include <string>
#include <vector>

class Creature;
class Player;
struct CombatSimRequest;
struct CreatureInfo;
struct FactionTemplateEntry;

namespace ai
{
    // runtime context of HuntProvider_GlobalTask
    struct HuntProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when pullTarget got a new unit:
        //   <Transition trigger="OnDelegate" source="Root|HuntProvider_GlobalTask::onPullTarget" target="Root|Pull"/>
        FSTAI_Delegate onPullTarget;
        ObjectGuid pullTarget;          // property "pullTarget": the unit to pull now, empty without one
        uint32 candidates = 0;          // property "candidates": the units that could be pulled (before the safety check)
        std::string unsafe;             // property "unsafe": what keeps the nearest candidate from being pulled (debug)
        float winChance = -1.0f;        // property "winChance": the last simulated fight's chance, 0..1 (-1: none yet)
        std::shared_ptr<CombatSimRequest> sim;     // the fight being simulated, against simTarget
        ObjectGuid simTarget;
        // property "huntLocation": a spawn point of fitting creatures further away, to walk to while nothing is in range
        STAI_Location huntLocation;
        // property "area" (bound, optional, e.g. to LevelingProvider_GlobalTask::huntArea): where to hunt; while the bot is
        // beyond AreaRadius of it huntLocation is the area itself, inside only spawn points within AreaRadius of it count
        STAI_Location area;
        // property "entries" (bound, optional, e.g. to QuestProvider_GlobalTask::objectiveEntries): with EntriesOnly
        // only creatures of these entries are hunted, whatever their level and in sight or not (none while it is empty)
        std::vector<uint32> entries;

        struct Visited
        {
            float x, y;
            uint32 atMs;
        };
        std::vector<Visited> visited;   // hunt locations reached lately, not chosen again for a while
        std::vector<std::pair<ObjectGuid, uint32>> ignored;    // units kept too long without a fight (unreachable), until
        uint32 targetSinceMs = 0;       // how long pullTarget has been the same unit
        uint32 sinceCheckMs = 0;
        uint32 sinceHuntCheckMs = 0;
        uint32 nowMs = 0;
    };

    // Global task that picks what a solo bot hunts: a living creature it may attack around it, of a fitting level, not
    // fighting anybody, not tapped by another player, no elite (unless allowed), no critter, civilian or npc with a
    // service, in sight (not needed with EntriesOnly), and safe to pull (no other living aggressive creature that is not
    // fighting within SafeRadius of it; with MinWinChance they may join while the CombatWinChanceSubsystem gives the
    // bot (and its group around) at least that chance against the unit and them; "unsafe" names what kept the nearest
    // one from being pulled, a unit lost to is ignored for a minute).
    // The closest one wins; a chosen unit is kept while it stays valid, for 30s at most (then it is ignored for 5 minutes:
    // the bot could not reach or pull it). Without one, huntLocation is the closest spawn
    // point (creature table) of such creatures beyond Range, not visited lately (with a bound area only inside it, a
    // new round once all were visited):
    //   <GlobalTasks>
    //     <Task class="HuntProvider_GlobalTask">
    //       <Range value="50"/>                yards searched for units, default 50
    //       <Levels below="5" above="1"/>      levels around the bot's, default 5 below and 1 above
    //       <Elite value="false"/>             elites may be pulled, default false
    //       <SafeRadius value="12"/>           yards around the unit without another idle aggressive creature, default 12
    //       <MinWinChance value="0.75"/>       0..1: adds are taken on while the simulated fight is won this often,
    //                                          default 0 (no adds)
    //       <HuntRange value="400"/>           yards searched for a hunt location, default 400
    //       <AreaRadius value="150"/>          yards around the bound area its spawn points are taken from, default 150
    //       <EntriesOnly value="true"/>        only the bound entries (quest targets), default false
    //       <Binding local="entries" external="Root|QuestProvider_GlobalTask::objectiveEntries"/>    with EntriesOnly
    //       <Binding local="area" external="Root|LevelingProvider_GlobalTask::huntArea"/>    optional
    //     </Task>
    //   </GlobalTasks>
    // Ticked: the unit every 500ms, the hunt location every 5s (at once when the bot reached it).
    class HuntProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Settings
        {
            float range = 50.0f;
            uint32 levelsBelow = 5;
            uint32 levelsAbove = 1;
            bool elite = false;
            float safeRadius = 12.0f;
            float minWinChance = 0.0f;
            float huntRange = 400.0f;
            float areaRadius = 150.0f;
            bool entriesOnly = false;
        };

        explicit HuntProvider_GlobalTask(const Settings& settings) : settings(settings) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "HuntProvider_GlobalTask"; }
        inline static std::string task_name = "HuntProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<HuntProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        // a creature template a bot of this faction and level may hunt: its levels within levelsBelow / levelsAbove of
        // the bot's, no elite (unless allowed), no critter, civilian or npc with a service, not friendly to the bot
        static bool IsHuntable(const FactionTemplateEntry* botFaction, uint32 level, const CreatureInfo* info, uint32 levelsBelow, uint32 levelsAbove, bool elite);
        static bool FitsLevel(uint32 level, uint32 minLevel, uint32 maxLevel, uint32 levelsBelow, uint32 levelsAbove);

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);

        bool FitsLevel(const Player* bot, uint32 minLevel, uint32 maxLevel) const;
        bool CanHunt(Player* bot, Creature* creature, const std::vector<uint32>& entries) const;
        // the idle aggressive creatures that would join a fight with the unit
        std::vector<Creature*> PullAdds(Player* bot, Creature* creature) const;
        void UpdateTarget(HuntProvider_GlobalTaskContext& context) const;
        void UpdateHuntLocation(HuntProvider_GlobalTaskContext& context) const;

        Settings settings;
    };
}
