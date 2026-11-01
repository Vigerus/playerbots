#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;

namespace ai
{
    // runtime context of LevelingProvider_GlobalTask (what has to outlive the tree, the bot's spot and its pay, is kept
    // per bot inside the provider: the NonCombat trees start again after every fight)
    struct LevelingProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when zoneId / huntArea changed:
        //   <Transition trigger="OnDelegate" source="Root|LevelingProvider_GlobalTask::onZoneChanged" target="Root"/>
        FSTAI_Delegate onZoneChanged;
        FSTAI_Delegate onAreaChanged;
        uint32 zoneId = 0;          // property "zoneId": the zone to level in (AreaTable), 0 without one
        std::string zoneName;       // property "zoneName"
        bool inZone = false;        // property "inZone": the bot is in that zone
        STAI_Location huntArea;     // property "huntArea": the spawn group to grind in that zone, unset without one
        uint32 areaLevel = 0;       // property "areaLevel": the average level of its creatures
        uint32 areas = 0;           // property "areas": the spawn groups of the zone fit for the bot
        uint32 xpPerHour = 0;       // property "xpPerHour": what the bot earned at huntArea (0 until it was there a while)
        uint32 sinceCheckMs = 0;
    };

    // Global task that plans where a solo bot levels: picks the zone for its level (the classic zone level ranges, its
    // faction's zones and the contested ones, on its continent for now: the zone it is in while it fits, else the one
    // whose range centers nearest its level, then the nearest), and in it the spawn group to grind: creature spawns
    // grouped by Cell yards, a group counts with MinCount huntable creatures (HuntProvider_GlobalTask::IsHuntable with
    // Levels) and no elite or much higher creature in or next to it, the one whose average level is nearest the bot's
    // wins (nearest the bot on a tie). A level up picks again (higher groups). Grinding that does not pay (after 5
    // minutes at the group less than a level per MaxHours hours) leaves the group for 20 minutes, the next best one is
    // taken; with every group of the zone left the zone is left for 30 minutes and the next fitting zone is taken:
    //   <GlobalTasks>
    //     <Task class="LevelingProvider_GlobalTask">
    //       <Levels below="4" above="1"/>    creature levels counted around the bot's, default 4 below and 1 above
    //       <Cell value="80"/>               yards of a spawn group, default 80
    //       <MinCount value="4"/>            huntable spawns a group needs, default 4
    //       <MaxHours value="0.75"/>         hours a level may take at most, default 0.75
    //     </Task>
    //     <Task class="HuntProvider_GlobalTask">
    //       <Binding local="area" external="Root|LevelingProvider_GlobalTask::huntArea"/>
    //     </Task>
    //   </GlobalTasks>
    // Ticked: checks every 5s. The spawn groups of a zone are worked out once per zone, level and faction, for every bot.
    class LevelingProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Settings
        {
            uint32 levelsBelow = 4;
            uint32 levelsAbove = 1;
            float cell = 80.0f;
            uint32 minCount = 4;
            float maxHours = 0.75f;
        };

        // a zone of the level table
        struct Zone
        {
            uint32 zoneId;
            uint32 mapId;
            const char* name;
            uint32 team;            // ALLIANCE, HORDE, 0: contested
            uint32 minLevel;
            uint32 maxLevel;
        };

        // a spawn group of a zone
        struct Area
        {
            float x, y, z;          // the huntable spawn nearest the group's middle
            uint32 count;           // huntable spawns
            float avgLevel;
        };

        explicit LevelingProvider_GlobalTask(const Settings& settings) : settings(settings) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "LevelingProvider_GlobalTask"; }
        inline static std::string task_name = "LevelingProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<LevelingProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        static const std::vector<Zone>& GetZones();

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);

        // the zone for the bot's level (preferred while it fits), skipping the ones it left (zone ids)
        const Zone* ChooseZone(Player* bot, uint32 preferred, const std::vector<uint32>& left) const;
        // the zone's spawn groups for a bot of this level and faction (worked out once, shared by every bot)
        const std::vector<Area>& GetAreas(Player* bot, const Zone& zone) const;
        void Update(LevelingProvider_GlobalTaskContext& context) const;

        Settings settings;
        // by zone, faction template and level; entries are never removed, so returned references stay valid
        mutable std::mutex areasLock;
        mutable std::unordered_map<uint64, std::vector<Area>> areasCache;
    };
}
