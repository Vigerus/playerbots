#pragma once

#include "Common.h"

#include <unordered_map>
#include <vector>

namespace ai
{
    struct STAI_SpawnPoint
    {
        float x, y, z;
    };

    // a quest giver spawn of a map
    struct STAI_GiverSpawn
    {
        uint32 entry;
        float x, y, z;
    };

    // What the questing providers need to know about quests, built once on first use from the creature / game object
    // tables, the quest relations and the loot templates, read only afterwards (any map thread may read it):
    //   const STAI_QuestIndex& index = STAI_QuestIndex::Get();
    // Only creatures give and take quests here (quest giver game objects are left out).
    class STAI_QuestIndex
    {
    public:
        static const STAI_QuestIndex& Get();

        // spawn points of a creature / game object entry on a map, empty without any
        const std::vector<STAI_SpawnPoint>& CreatureSpawns(uint32 mapId, uint32 entry) const;
        const std::vector<STAI_SpawnPoint>& ObjectSpawns(uint32 mapId, uint32 entry) const;
        // the spawns of a map whose creature starts a quest
        const std::vector<STAI_GiverSpawn>& GiverSpawns(uint32 mapId) const;
        // creature entries a quest is turned in at
        const std::vector<uint32>& Takers(uint32 questId) const;
        // where a quest item drops: creature entries and game object entries whose loot has it
        const std::vector<uint32>& ItemCreatures(uint32 itemId) const;
        const std::vector<uint32>& ItemObjects(uint32 itemId) const;

    private:
        void Build();

        static uint64 Key(uint32 mapId, uint32 entry) { return (uint64(mapId) << 32) | entry; }

        std::unordered_map<uint64, std::vector<STAI_SpawnPoint>> creatureSpawns;
        std::unordered_map<uint64, std::vector<STAI_SpawnPoint>> objectSpawns;
        std::unordered_map<uint32, std::vector<STAI_GiverSpawn>> giverSpawns;
        std::unordered_map<uint32, std::vector<uint32>> takers;
        std::unordered_map<uint32, std::vector<uint32>> itemCreatures;
        std::unordered_map<uint32, std::vector<uint32>> itemObjects;
    };
}
