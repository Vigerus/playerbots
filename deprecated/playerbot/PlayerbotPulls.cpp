#include "PlayerbotPulls.h"

#include "Chat/Chat.h"
#include "Database/DatabaseEnv.h"
#include "Entities/Creature.h"
#include "Entities/CreatureLinkingMgr.h"
#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"
#include "Groups/Group.h"
#include "Maps/Map.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Maps/SpawnGroupDefines.h"
#include "Server/DBCStores.h"
#include "Server/WorldPacket.h"
#include "Server/WorldSession.h"
#include "Util/Timer.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

using namespace ai;

namespace
{
    // spawns this close (and at about the same height) are one pack
    constexpr float packLinkRange = 10.0f;
    constexpr float packLinkHeight = 5.0f;
    // the plan checks the active pull this often
    constexpr uint32 checkInterval = 500;
    // names of the entries in a generated name
    constexpr size_t nameEntries = 3;

    std::mutex packsLock;
    std::unordered_map<uint32, std::shared_ptr<const std::vector<DungeonPack>>> packsByMap;

    struct UnionFind
    {
        std::vector<size_t> parent;

        explicit UnionFind(size_t size) : parent(size)
        {
            for (size_t i = 0; i < size; ++i)
                parent[i] = i;
        }

        size_t Find(size_t i)
        {
            while (parent[i] != i)
            {
                parent[i] = parent[parent[i]];
                i = parent[i];
            }
            return i;
        }

        void Join(size_t a, size_t b)
        {
            a = Find(a);
            b = Find(b);
            if (a != b)
                parent[std::max(a, b)] = std::min(a, b);
        }
    };

    // creatures the bots fight: no critters, triggers (uninteractible, invisible), creatures friendly to players
    bool IsFought(CreatureInfo const* info)
    {
        if (!info || info->CreatureType == CREATURE_TYPE_CRITTER)
            return false;
        if ((info->UnitFlags & UNIT_FLAG_UNINTERACTIBLE) || (info->ExtraFlags & CREATURE_EXTRA_FLAG_INVISIBLE))
            return false;
        if (FactionTemplateEntry const* faction = sFactionTemplateStore.LookupEntry(info->Faction))
            if (faction->friendGroupMask & FACTION_GROUP_MASK_PLAYER)
                return false;
        return true;
    }

    // walking distance estimate: height counts more (another floor is far)
    float WalkDistanceSq(float x1, float y1, float z1, float x2, float y2, float z2)
    {
        const float dx = x1 - x2, dy = y1 - y2, dz = (z1 - z2) * 3.0f;
        return dx * dx + dy * dy + dz * dz;
    }

    std::vector<uint32> EntriesOf(const std::vector<uint32>& guids)
    {
        std::vector<uint32> entries;
        for (uint32 guid : guids)
            if (CreatureData const* data = sObjectMgr.GetCreatureData(guid))
                entries.push_back(data->id);
        return entries;
    }

    std::vector<uint32> ParseGuids(const std::string& text)
    {
        std::vector<uint32> guids;
        std::istringstream in(text);
        std::string part;
        while (std::getline(in, part, ','))
            if (const uint32 guid = uint32(std::strtoul(part.c_str(), nullptr, 10)))
                guids.push_back(guid);
        std::sort(guids.begin(), guids.end());
        return guids;
    }
}

// ---------------------------------------------------------------- packs

std::shared_ptr<const std::vector<DungeonPack>> DungeonPacks::Get(uint32 mapId)
{
    {
        std::lock_guard<std::mutex> lock(packsLock);
        auto itr = packsByMap.find(mapId);
        if (itr != packsByMap.end())
            return itr->second;
    }

    // built outside the lock: the creature data is read only at runtime
    auto packs = std::make_shared<const std::vector<DungeonPack>>(Generate(mapId));
    std::lock_guard<std::mutex> lock(packsLock);
    return packsByMap.emplace(mapId, packs).first->second;
}

std::string DungeonPacks::MakeName(const std::vector<uint32>& entries)
{
    std::map<uint32, uint32> counts;
    for (uint32 entry : entries)
        ++counts[entry];

    // the highest rank (boss, elite) first, then the most frequent
    std::vector<std::pair<uint32, uint32>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const std::pair<uint32, uint32>& a, const std::pair<uint32, uint32>& b)
    {
        CreatureInfo const* infoA = ObjectMgr::GetCreatureTemplate(a.first);
        CreatureInfo const* infoB = ObjectMgr::GetCreatureTemplate(b.first);
        const uint32 rankA = infoA ? infoA->Rank : 0, rankB = infoB ? infoB->Rank : 0;
        if (rankA != rankB)
            return rankA > rankB;
        return a.second > b.second;
    });

    std::ostringstream out;
    for (size_t i = 0; i < sorted.size() && i < nameEntries; ++i)
    {
        CreatureInfo const* info = ObjectMgr::GetCreatureTemplate(sorted[i].first);
        out << (i ? ", " : "") << (info && info->Name ? info->Name : "creature " + std::to_string(sorted[i].first));
        if (sorted[i].second > 1)
            out << " x" << sorted[i].second;
    }
    if (sorted.size() > nameEntries)
        out << ", ...";
    return out.str();
}

bool DungeonPacks::IsAlive(Map* map, uint32 dbGuid)
{
    if (Creature* creature = map->GetCreature(dbGuid))     // prefers a living one
        return creature->IsAlive();

    CreatureData const* data = sObjectMgr.GetCreatureData(dbGuid);
    if (!data)
        return false;
    // not in the map: killed and removed or not spawned while its grid is loaded, else waiting for the grid
    if (map->IsLoaded(data->posX, data->posY))
        return false;

    MapPersistentState* state = map->GetPersistentState();
    return !state || state->GetCreatureRespawnTime(dbGuid) <= time(nullptr);
}

std::vector<DungeonPack> DungeonPacks::Generate(uint32 mapId)
{
    std::vector<DungeonPackSpawn> spawns;
    auto worker = [&](CreatureDataPair const& pair) -> bool
    {
        CreatureData const& data = pair.second;
        if (data.mapid == mapId && IsFought(ObjectMgr::GetCreatureTemplate(data.id)))
            spawns.push_back({ pair.first, data.id, data.posX, data.posY, data.posZ });
        return false;
    };
    sObjectMgr.DoCreatureData(worker);

    std::unordered_map<uint32, size_t> byGuid;
    for (size_t i = 0; i < spawns.size(); ++i)
        byGuid[spawns[i].dbGuid] = i;

    UnionFind sets(spawns.size());

    // close to each other
    for (size_t i = 0; i < spawns.size(); ++i)
        for (size_t j = i + 1; j < spawns.size(); ++j)
        {
            const float dx = spawns[i].x - spawns[j].x, dy = spawns[i].y - spawns[j].y;
            if (dx * dx + dy * dy <= packLinkRange * packLinkRange && std::abs(spawns[i].z - spawns[j].z) <= packLinkHeight)
                sets.Join(i, j);
        }

    // one spawn group
    if (std::shared_ptr<SpawnGroupEntryContainer> groups = sObjectMgr.GetSpawnGroupContainer())
    {
        std::unordered_map<uint32, size_t> firstOfGroup;
        for (size_t i = 0; i < spawns.size(); ++i)
        {
            auto itr = groups->spawnGroupByGuidMap.find(std::make_pair(spawns[i].dbGuid, uint32(TYPEID_UNIT)));
            if (itr == groups->spawnGroupByGuidMap.end() || !itr->second)
                continue;
            auto first = firstOfGroup.emplace(itr->second->Id, i).first;
            sets.Join(first->second, i);
        }
    }

    // linked to a master (creature_linking): the master by its guid, else the nearest of its entry
    for (size_t i = 0; i < spawns.size(); ++i)
    {
        CreatureLinkingInfo const* link = sCreatureLinkingMgr.GetLinkedTriggerInformation(spawns[i].entry, spawns[i].dbGuid, mapId);
        if (!link)
            continue;
        if (link->masterDBGuid)
        {
            auto itr = byGuid.find(link->masterDBGuid);
            if (itr != byGuid.end())
                sets.Join(itr->second, i);
            continue;
        }

        size_t nearest = spawns.size();
        float nearestDistance = 0.0f;
        for (size_t j = 0; j < spawns.size(); ++j)
        {
            if (spawns[j].entry != link->masterId)
                continue;
            const float distance = WalkDistanceSq(spawns[i].x, spawns[i].y, spawns[i].z, spawns[j].x, spawns[j].y, spawns[j].z);
            if (nearest == spawns.size() || distance < nearestDistance)
            {
                nearest = j;
                nearestDistance = distance;
            }
        }
        if (nearest != spawns.size())
            sets.Join(nearest, i);
    }

    std::map<size_t, DungeonPack> byRoot;
    for (size_t i = 0; i < spawns.size(); ++i)
        byRoot[sets.Find(i)].spawns.push_back(spawns[i]);

    std::vector<DungeonPack> unordered;
    for (auto& [root, pack] : byRoot)
    {
        std::vector<uint32> entries;
        pack.id = pack.spawns.front().dbGuid;
        for (const DungeonPackSpawn& spawn : pack.spawns)
        {
            pack.id = std::min(pack.id, spawn.dbGuid);
            pack.x += spawn.x;
            pack.y += spawn.y;
            pack.z += spawn.z;
            entries.push_back(spawn.entry);
        }
        pack.x /= pack.spawns.size();
        pack.y /= pack.spawns.size();
        pack.z /= pack.spawns.size();
        pack.name = MakeName(entries);
        unordered.push_back(std::move(pack));
    }

    // the walking order: from the entrance (the teleport target into the map), always the nearest pack left
    float x = 0.0f, y = 0.0f, z = 0.0f;
    AreaTrigger const* entrance = sObjectMgr.GetMapEntranceTrigger(mapId);
    if (entrance && entrance->target_mapId == mapId)
    {
        x = entrance->target_X;
        y = entrance->target_Y;
        z = entrance->target_Z;
    }
    else if (!unordered.empty())
    {
        x = unordered.front().x;
        y = unordered.front().y;
        z = unordered.front().z;
    }

    std::vector<DungeonPack> packs;
    std::vector<bool> taken(unordered.size(), false);
    for (size_t n = 0; n < unordered.size(); ++n)
    {
        size_t best = unordered.size();
        float bestDistance = 0.0f;
        for (size_t i = 0; i < unordered.size(); ++i)
        {
            if (taken[i])
                continue;
            const float distance = WalkDistanceSq(x, y, z, unordered[i].x, unordered[i].y, unordered[i].z);
            if (best == unordered.size() || distance < bestDistance)
            {
                best = i;
                bestDistance = distance;
            }
        }
        taken[best] = true;
        x = unordered[best].x;
        y = unordered[best].y;
        z = unordered[best].z;
        packs.push_back(std::move(unordered[best]));
    }

    sLog.outString("PlayerbotPulls: map %u: %u packs from %u creatures", mapId, uint32(packs.size()), uint32(spawns.size()));
    return packs;
}

// ---------------------------------------------------------------- plan

bool PlayerbotPullPlan::QueueCommand(const std::string& text)
{
    if (text.rfind("plan ", 0) != 0 && text.rfind("planner ", 0) != 0)
        return false;

    std::lock_guard<std::mutex> lock(queueLock);
    queue.push_back(text);
    return true;
}

bool PlayerbotPullPlan::GetActive(uint32 mapId, uint32 instanceId, ActivePull& pull) const
{
    std::lock_guard<std::mutex> lock(planLock);
    const Plan* plan = CurrentPlan();
    if (!known || !plan || mapId != this->mapId || instanceId != this->instanceId || active < 0 || active >= int32(plan->pulls.size()))
        return false;

    const DungeonPull& source = plan->pulls[active];
    pull.id = source.id;
    pull.index = uint32(active) + 1;
    pull.count = uint32(plan->pulls.size());
    pull.name = source.name;
    pull.cleared = activeCleared;
    pull.hasPosition = activeCleared ? source.hasCleared : source.hasStart;
    pull.position = activeCleared ? source.cleared : source.start;
    pull.path = activeCleared ? std::vector<DungeonPlanPoint>() : source.path;
    return true;
}

void PlayerbotPullPlan::Update(Player* master)
{
    const uint32 now = WorldTimer::getMSTime();
    const uint32 elapsed = lastUpdate ? WorldTimer::getMSTimeDiff(lastUpdate, now) : 0;
    lastUpdate = now;

    std::vector<std::string> commands;
    {
        std::lock_guard<std::mutex> lock(queueLock);
        commands.swap(queue);
    }

    if (!master->IsInWorld() || master->IsBeingTeleported())
        return;

    Map* map = master->GetMap();
    std::lock_guard<std::mutex> lock(planLock);
    if (!known || map->GetId() != mapId || map->GetInstanceId() != instanceId)
        Enter(master);

    for (const std::string& command : commands)
        HandleCommand(master, command);

    checkTimer += elapsed;
    if (checkTimer < checkInterval)
        return;
    checkTimer = 0;

    // the state of the active pull, the alive count for the addon
    const Plan* plan = CurrentPlan();
    if (inDungeon && plan && active >= 0 && active < int32(plan->pulls.size()))
        activeCleared = IsCleared(map, plan->pulls[active]);
    SendStatus(master);
}

void PlayerbotPullPlan::Enter(Player* master)
{
    Map* map = master->GetMap();
    known = true;
    mapId = map->GetId();
    instanceId = map->GetInstanceId();
    inDungeon = map->IsDungeon();
    active = -1;
    activeCleared = false;
    lastStatus.clear();

    const Plan* plan = CurrentPlan();
    if (!inDungeon || !plan)
        return;

    active = FindAlive(map, 0);
    Tell(master, "plan \"" + plan->name + "\" for " + map->GetMapName() + ": " + std::to_string(plan->pulls.size()) + " pulls");
}

void PlayerbotPullPlan::HandleCommand(Player* master, const std::string& text)
{
    std::istringstream in(text);
    std::string word, command;
    in >> word >> command;

    if (word == "planner")
    {
        uint32 spawnMap = 0;
        if (command == "spawns" && (in >> spawnMap))
            SendSpawns(master, spawnMap);
        return;
    }

    Map* map = master->GetMap();
    if (command == "begin")
    {
        uint32 count = 0;
        in >> receivingMap >> count;
        receiving = true;
        receivingCount = count;
        received = Plan();
        std::getline(in >> std::ws, received.name);
        received.pulls.resize(std::min<uint32>(count, 500));
    }
    else if (command == "guids" || command == "start" || command == "cleared" || command == "path")
    {
        uint32 index = 0;
        if (!receiving || !(in >> index) || index >= received.pulls.size())
            return;

        DungeonPull& pull = received.pulls[index];
        if (command == "guids")
        {
            std::string list;
            in >> list;
            const std::vector<uint32> guids = ParseGuids(list);
            pull.guids.insert(pull.guids.end(), guids.begin(), guids.end());
            return;
        }

        DungeonPlanPoint point;
        while (in >> point.x >> point.y >> point.z)
        {
            if (command == "start")
            {
                pull.hasStart = true;
                pull.start = point;
            }
            else if (command == "cleared")
            {
                pull.hasCleared = true;
                pull.cleared = point;
            }
            else
                pull.path.push_back(point);
        }
    }
    else if (command == "end")
    {
        if (!receiving)
            return;
        receiving = false;

        // pulls without creatures are dropped; the name from the creatures
        Plan plan;
        plan.name = received.name.empty() ? "plan" : received.name;
        for (DungeonPull& pull : received.pulls)
        {
            if (pull.guids.empty())
                continue;
            std::sort(pull.guids.begin(), pull.guids.end());
            pull.guids.erase(std::unique(pull.guids.begin(), pull.guids.end()), pull.guids.end());
            pull.id = pull.guids.front();
            pull.name = DungeonPacks::MakeName(EntriesOf(pull.guids));
            plan.pulls.push_back(std::move(pull));
        }
        plans[receivingMap] = std::move(plan);

        const Plan& installed = plans[receivingMap];
        MapEntry const* mapEntry = sMapStore.LookupEntry(receivingMap);
        Tell(master, "plan \"" + installed.name + "\" uploaded for " + (mapEntry ? mapEntry->name[0] : std::to_string(receivingMap)) +
            ": " + std::to_string(installed.pulls.size()) + " pulls");

        // in that dungeon now: from its first pull that is not cleared
        if (inDungeon && receivingMap == mapId)
        {
            active = FindAlive(map, 0);
            activeCleared = false;
        }
    }
    else if (command == "remove")
    {
        uint32 removeMap = 0;
        in >> removeMap;
        plans.erase(removeMap);
        if (removeMap == mapId)
            active = -1;
        Tell(master, "plan removed");
    }
    else if (command == "next" || command == "prev")
    {
        const Plan* plan = CurrentPlan();
        if (!inDungeon || !plan || plan->pulls.empty())
        {
            Tell(master, "no plan for this dungeon (upload one in the Bot Dungeon Planner)");
            return;
        }
        if (command == "next")
        {
            if (active + 1 >= int32(plan->pulls.size()))
            {
                Tell(master, "that was the last pull of the plan");
                return;
            }
            Activate(master, active + 1);
        }
        else
            Activate(master, std::max(0, active - 1));
    }
    else if (command == "status")
    {
        const Plan* plan = CurrentPlan();
        if (!plan || active < 0)
            Tell(master, "no active pull");
        else
            Tell(master, "pull " + std::to_string(active + 1) + "/" + std::to_string(plan->pulls.size()) + ": " + plan->pulls[active].name);
    }
    else if (command == "sync")
        lastStatus.clear();
}

void PlayerbotPullPlan::Activate(Player* master, int32 index)
{
    const Plan* plan = CurrentPlan();
    if (!plan || index < 0 || index >= int32(plan->pulls.size()))
        return;

    active = index;
    activeCleared = IsCleared(master->GetMap(), plan->pulls[active]);
    Tell(master, "pull " + std::to_string(active + 1) + "/" + std::to_string(plan->pulls.size()) + ": " + plan->pulls[active].name +
        (activeCleared ? " (cleared)" : ""));
    SendStatus(master);
}

int32 PlayerbotPullPlan::FindAlive(Map* map, int32 index) const
{
    const Plan* plan = CurrentPlan();
    if (!plan)
        return -1;
    for (int32 i = std::max(0, index); i < int32(plan->pulls.size()); ++i)
        if (!IsCleared(map, plan->pulls[i]))
            return i;
    return plan->pulls.empty() ? -1 : int32(plan->pulls.size()) - 1;
}

bool PlayerbotPullPlan::IsCleared(Map* map, const DungeonPull& pull) const
{
    for (uint32 guid : pull.guids)
        if (DungeonPacks::IsAlive(map, guid))
            return false;
    return true;
}

const PlayerbotPullPlan::Plan* PlayerbotPullPlan::CurrentPlan() const
{
    auto itr = plans.find(mapId);
    return itr != plans.end() && !itr->second.pulls.empty() ? &itr->second : nullptr;
}

void PlayerbotPullPlan::Tell(Player* master, const std::string& text) const
{
    if (master->GetSession())
        ChatHandler(master).PSendSysMessage("|cff8fd8ffBot plan:|r %s", text.c_str());
}

void PlayerbotPullPlan::SendStatus(Player* master)
{
    if (!master->GetSession())
        return;

    const Plan* plan = inDungeon ? CurrentPlan() : nullptr;
    std::ostringstream out;
    if (plan && active >= 0 && active < int32(plan->pulls.size()))
    {
        const DungeonPull& pull = plan->pulls[active];
        uint32 alive = 0;
        for (uint32 guid : pull.guids)
            if (DungeonPacks::IsAlive(master->GetMap(), guid))
                ++alive;
        out << active + 1 << "^" << plan->pulls.size() << "^" << pull.name << "^" << alive << "^" << pull.guids.size()
            << "^" << (activeCleared ? 1 : 0) << "^" << plan->name;
    }
    else
        out << "0^0^^0^0^0^";

    // only changes (sync forgets the last one)
    if (out.str() == lastStatus)
        return;
    lastStatus = out.str();

    std::string text = "BOTPL\t" + lastStatus;
    if (text.size() > 250)
        text.resize(250);
    WorldPacket data;
    ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, text.c_str(), LANG_ADDON, CHAT_TAG_NONE, master->GetObjectGuid(), master->GetName());
    master->GetSession()->SendPacket(data);
}

void PlayerbotPullPlan::SendSpawns(Player* master, uint32 spawnMap)
{
    if (!master->GetSession())
        return;

    std::shared_ptr<const std::vector<DungeonPack>> packs = DungeonPacks::Get(spawnMap);
    std::ostringstream payload;
    payload << std::fixed << std::setprecision(1) << "M^" << spawnMap;

    std::set<uint32> entries;
    for (const DungeonPack& pack : *packs)
        for (const DungeonPackSpawn& spawn : pack.spawns)
            entries.insert(spawn.entry);
    for (uint32 entry : entries)
    {
        CreatureInfo const* info = ObjectMgr::GetCreatureTemplate(entry);
        std::string name = info && info->Name ? info->Name : "creature " + std::to_string(entry);
        std::replace(name.begin(), name.end(), '^', ' ');
        std::replace(name.begin(), name.end(), '~', ' ');
        payload << "~E^" << entry << "^" << name << "^" << (info ? info->MaxLevel : 0) << "^" << (info ? info->Rank : 0);
    }
    for (size_t i = 0; i < packs->size(); ++i)
        for (const DungeonPackSpawn& spawn : (*packs)[i].spawns)
            payload << "~C^" << spawn.dbGuid << "^" << spawn.entry << "^" << spawn.x << "^" << spawn.y << "^" << spawn.z << "^" << (*packs)[i].id;

    // addon messages are limited to 255 chars: chunks, sent as party addon messages from the master itself
    const size_t chunkSize = 220;
    const std::string text = payload.str();
    const size_t count = std::max<size_t>(1, (text.size() + chunkSize - 1) / chunkSize);
    const uint32 seq = ++spawnSeq % 256;
    for (size_t i = 0; i < count; ++i)
    {
        std::ostringstream chunk;
        chunk << "BOTPS\tS" << seq << ":" << (i + 1) << "/" << count << ":" << text.substr(i * chunkSize, chunkSize);

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, chunk.str().c_str(), LANG_ADDON, CHAT_TAG_NONE, master->GetObjectGuid(), master->GetName());
        master->GetSession()->SendPacket(data);
    }
}
