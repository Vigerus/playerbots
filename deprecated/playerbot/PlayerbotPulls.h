#pragma once

#include "Platform/Define.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Map;
class Player;

namespace ai
{
    // a creature spawn of a pack (the creature data of the map)
    struct DungeonPackSpawn
    {
        uint32 dbGuid;
        uint32 entry;
        float x, y, z;
    };

    // creatures of a dungeon that are fought together: linked or in one spawn group, or standing close to each other
    struct DungeonPack
    {
        uint32 id = 0;          // its lowest creature db guid
        std::string name;       // "<creature name> x<count>, ...", the boss first
        std::vector<DungeonPackSpawn> spawns;
        float x = 0.0f, y = 0.0f, z = 0.0f;     // the centre
    };

    // The packs of a dungeon map, generated once per map from its creature spawns (hostile, no critters or triggers),
    // ordered the way a group walks the dungeon: from the entrance, always to the nearest pack not taken yet. The Bot
    // Dungeon Planner addon gets them as its creature dots (the pack is its click-to-add group).
    // Shared, read only once built: any thread.
    class DungeonPacks
    {
    public:
        static std::shared_ptr<const std::vector<DungeonPack>> Get(uint32 mapId);
        // "<name> x<count>, ..." of the creature entries, the most frequent first
        static std::string MakeName(const std::vector<uint32>& entries);
        // the spawn is alive in the map: a loaded creature that lives, or one not loaded while its grid is not either
        // and that has no respawn time pending (the map's thread)
        static bool IsAlive(Map* map, uint32 dbGuid);

    private:
        static std::vector<DungeonPack> Generate(uint32 mapId);
    };

    struct DungeonPlanPoint
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    // a pull of a plan: creatures killed together, where the bots wait for it (start), where they go once it is dead
    // (cleared) and the way there from the previous pull (path, walked before the start position)
    struct DungeonPull
    {
        uint32 id = 0;                  // its lowest creature db guid (keys the bots' rtsc spots)
        std::string name;
        std::vector<uint32> guids;      // creature db guids
        bool hasStart = false;
        DungeonPlanPoint start;
        bool hasCleared = false;
        DungeonPlanPoint cleared;
        std::vector<DungeonPlanPoint> path;
    };

    // the active pull as the bots read it
    struct ActivePull
    {
        uint32 id = 0;
        uint32 index = 0;               // 1 based
        uint32 count = 0;
        std::string name;
        bool cleared = false;           // its creatures are dead: the bots go to the cleared position
        bool hasPosition = false;       // the plan's position for the state (start / cleared)
        DungeonPlanPoint position;
        std::vector<DungeonPlanPoint> path;     // before it is cleared: the way to the start position
    };

    // The dungeon plans of a master, made in the Bot Dungeon Planner addon and uploaded per dungeon (kept until the
    // master logs out; the addon uploads its selected plan again on zoning in). In the planned dungeon one pull is
    // active: the bots walk its path, wait at its start position, and once its creatures are dead go to its cleared
    // position. Next (DungeonStage addon) activates the next pull. An rtsc move saves the bot's own spot for the active
    // pull and its state (PlayerbotDungeonPositions), which wins over the plan's position.
    // Addon messages (prefix "BOT"), from the master:
    //   plan begin <map> <pulls> <plan name>     a new plan for the map, its pulls follow
    //   plan guids <pull> <guid,guid,...>        creatures of pull (0 based), appended
    //   plan start <pull> <x> <y> <z>            the start position     plan cleared <pull> <x> <y> <z>
    //   plan path <pull> <x> <y> <z> [...]       waypoints of the way to the start position, appended
    //   plan end                                 the plan is complete: it replaces the map's plan
    //   plan remove <map>                        forgets the map's plan
    //   plan next / prev / sync                  the active pull / only the status
    //   planner spawns <map>                     the creature dots of the map (BOTPS chunks)
    // The status goes to the master as the "BOTPL" addon message (DungeonStage), on every change:
    //   <active index or 0>^<count>^<active name>^<alive>^<total>^<cleared 0|1>^<plan name>
    // The creature dots as "BOTPS" addon messages, "S<seq>:<index>/<count>:<payload part>", the payload
    //   M^<map>~E^<entry>^<name>^<level>^<rank>~...~C^<guid>^<entry>^<x>^<y>^<z>^<pack>~...
    // Messages are queued from the chat and run in the master's update (Update), where the bots read the plan
    // (GetActive): the map's thread.
    class PlayerbotPullPlan
    {
    public:
        // the "BOT" addon message (or typed "plan next|prev|status"), from the chat handler; true when it was one
        bool QueueCommand(const std::string& text);
        // the master's update
        void Update(Player* master);
        // the active pull of the master's dungeon (map / instance), false without one
        bool GetActive(uint32 mapId, uint32 instanceId, ActivePull& pull) const;

    private:
        struct Plan
        {
            std::string name;
            std::vector<DungeonPull> pulls;
        };

        void Enter(Player* master);
        void HandleCommand(Player* master, const std::string& command);
        void Activate(Player* master, int32 index);
        // the first pull from index on that is not cleared, -1 none
        int32 FindAlive(Map* map, int32 index) const;
        bool IsCleared(Map* map, const DungeonPull& pull) const;
        const Plan* CurrentPlan() const;
        void Tell(Player* master, const std::string& text) const;
        void SendStatus(Player* master);
        void SendSpawns(Player* master, uint32 mapId);

        std::mutex queueLock;
        std::vector<std::string> queue;

        mutable std::mutex planLock;    // the plans: written in the master's update, read by GetActive
        std::map<uint32, Plan> plans;   // per map
        bool receiving = false;
        uint32 receivingMap = 0;
        uint32 receivingCount = 0;
        Plan received;

        bool known = false;             // mapId / instanceId were entered once
        uint32 mapId = 0;
        uint32 instanceId = 0;
        bool inDungeon = false;
        int32 active = -1;
        bool activeCleared = false;     // the creatures of the active pull are dead
        std::string lastStatus;         // the last BOTPL text sent
        uint32 checkTimer = 0;
        uint32 lastUpdate = 0;          // WorldTimer ms of the last Update
        uint32 spawnSeq = 0;
    };
}
