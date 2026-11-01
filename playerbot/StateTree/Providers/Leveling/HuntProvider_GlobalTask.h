#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Creature;
class Player;
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
        // property "huntLocation": a spawn point of fitting creatures further away, to walk to while nothing is in range
        STAI_Location huntLocation;
        // property "area" (bound, optional, e.g. to LevelingProvider_GlobalTask::huntArea): where to hunt; while the bot is
        // beyond AreaRadius of it huntLocation is the area itself, inside only spawn points within AreaRadius of it count
        STAI_Location area;

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
    // service, in sight, and safe to pull (no other aggressive creature that is not fighting within SafeRadius of it).
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
    //       <HuntRange value="400"/>           yards searched for a hunt location, default 400
    //       <AreaRadius value="150"/>          yards around the bound area its spawn points are taken from, default 150
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
            float huntRange = 400.0f;
            float areaRadius = 150.0f;
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
        bool CanHunt(Player* bot, Creature* creature) const;
        bool IsSafeToPull(Player* bot, Creature* creature) const;
        void UpdateTarget(HuntProvider_GlobalTaskContext& context) const;
        void UpdateHuntLocation(HuntProvider_GlobalTaskContext& context) const;

        Settings settings;
    };
}
