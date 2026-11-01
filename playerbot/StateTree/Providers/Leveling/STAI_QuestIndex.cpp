#include "STAI_QuestIndex.h"

#include "playerbot/LootAccess.h"

#include "Entities/GameObject.h"
#include "Globals/ObjectMgr.h"
#include "Server/SQLStorages.h"

#include <algorithm>
#include <mutex>
#include <unordered_set>

using namespace ai;

namespace
{
    const std::vector<STAI_SpawnPoint> noSpawns;
    const std::vector<STAI_GiverSpawn> noGivers;
    const std::vector<uint32> noEntries;

    template <class Map>
    const typename Map::mapped_type& FindOr(const Map& map, const typename Map::key_type& key, const typename Map::mapped_type& none)
    {
        auto found = map.find(key);
        return found == map.end() ? none : found->second;
    }

    void AddUnique(std::vector<uint32>& entries, uint32 entry)
    {
        if (std::find(entries.begin(), entries.end(), entry) == entries.end())
            entries.push_back(entry);
    }

    // the item ids a loot template drops (not its references)
    template <class Visit>
    void ForEachLootItem(const LootTemplateAccess* loot, Visit visit)
    {
        if (!loot)
            return;
        auto visitList = [&visit](const LootStoreItemList& list)
        {
            for (const LootStoreItem& item : list)
                if (item.mincountOrRef > 0)
                    visit(item.itemid);
        };
        visitList(loot->Entries);
        for (const LootLootGroupAccess& group : loot->Groups)
        {
            visitList(group.ExplicitlyChanced);
            visitList(group.EqualChanced);
        }
    }
}

const STAI_QuestIndex& STAI_QuestIndex::Get()
{
    static STAI_QuestIndex index;
    static std::once_flag built;
    std::call_once(built, []() { index.Build(); });
    return index;
}

void STAI_QuestIndex::Build()
{
    struct CreatureWorker
    {
        STAI_QuestIndex& index;
        bool operator()(CreatureDataPair const& pair)
        {
            const CreatureData& data = pair.second;
            if (data.id)    // 0: the entry comes from a spawn group
                index.creatureSpawns[Key(data.mapid, data.id)].push_back({ data.posX, data.posY, data.posZ });
            return false;
        }
    } creatureWorker{ *this };
    sObjectMgr.DoCreatureData(creatureWorker);

    struct ObjectWorker
    {
        STAI_QuestIndex& index;
        bool operator()(GameObjectDataPair const& pair)
        {
            const GameObjectData& data = pair.second;
            if (data.id)
                index.objectSpawns[Key(data.mapid, data.id)].push_back({ data.posX, data.posY, data.posZ });
            return false;
        }
    } objectWorker{ *this };
    sObjectMgr.DoGOData(objectWorker);

    // givers and takers of the spawned creatures
    std::unordered_set<uint32> takerEntries;
    for (const auto& [key, spots] : creatureSpawns)
    {
        const uint32 mapId = uint32(key >> 32);
        const uint32 entry = uint32(key & 0xFFFFFFFF);
        const QuestRelationsMapBounds givers = sObjectMgr.GetCreatureQuestRelationsMapBounds(entry);
        if (givers.first != givers.second)
            for (const STAI_SpawnPoint& spot : spots)
                giverSpawns[mapId].push_back({ entry, spot.x, spot.y, spot.z });

        if (!takerEntries.insert(entry).second)
            continue;
        const QuestRelationsMapBounds involved = sObjectMgr.GetCreatureQuestInvolvedRelationsMapBounds(entry);
        for (auto itr = involved.first; itr != involved.second; ++itr)
            AddUnique(takers[itr->second], entry);
    }

    // the loot sources of the items quests ask for
    std::unordered_set<uint32> questItems;
    for (const auto& [questId, quest] : sObjectMgr.GetQuestTemplates())
        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->ReqItemId[i])
                questItems.insert(quest->ReqItemId[i]);

    for (uint32 entry = 1; entry < sCreatureStorage.GetMaxEntry(); ++entry)
        if (sCreatureStorage.LookupEntry<CreatureInfo>(entry))
            ForEachLootItem(GetLootTemplate(ObjectGuid(HIGHGUID_UNIT, entry, uint32(1)), LOOT_CORPSE), [&](uint32 itemId)
            {
                if (questItems.count(itemId))
                    AddUnique(itemCreatures[itemId], entry);
            });

    for (uint32 entry = 1; entry < sGOStorage.GetMaxEntry(); ++entry)
        if (sGOStorage.LookupEntry<GameObjectInfo>(entry))
            ForEachLootItem(GetLootTemplate(ObjectGuid(HIGHGUID_GAMEOBJECT, entry, uint32(1)), LOOT_CORPSE), [&](uint32 itemId)
            {
                if (questItems.count(itemId))
                    AddUnique(itemObjects[itemId], entry);
            });
}

const std::vector<STAI_SpawnPoint>& STAI_QuestIndex::CreatureSpawns(uint32 mapId, uint32 entry) const
{
    return FindOr(creatureSpawns, Key(mapId, entry), noSpawns);
}

const std::vector<STAI_SpawnPoint>& STAI_QuestIndex::ObjectSpawns(uint32 mapId, uint32 entry) const
{
    return FindOr(objectSpawns, Key(mapId, entry), noSpawns);
}

const std::vector<STAI_GiverSpawn>& STAI_QuestIndex::GiverSpawns(uint32 mapId) const
{
    return FindOr(giverSpawns, mapId, noGivers);
}

const std::vector<uint32>& STAI_QuestIndex::Takers(uint32 questId) const
{
    return FindOr(takers, questId, noEntries);
}

const std::vector<uint32>& STAI_QuestIndex::ItemCreatures(uint32 itemId) const
{
    return FindOr(itemCreatures, itemId, noEntries);
}

const std::vector<uint32>& STAI_QuestIndex::ItemObjects(uint32 itemId) const
{
    return FindOr(itemObjects, itemId, noEntries);
}
