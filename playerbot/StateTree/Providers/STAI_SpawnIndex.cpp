#include "STAI_SpawnIndex.h"

#include "Globals/ObjectMgr.h"

#include <mutex>
#include <unordered_map>

using namespace ai;

const std::vector<STAI_Spawn>& ai::STAI_GetSpawns(uint32 mapId)
{
    static std::unordered_map<uint32, std::vector<STAI_Spawn>> spawns;
    static std::once_flag built;
    std::call_once(built, []()
    {
        struct Worker
        {
            std::unordered_map<uint32, std::vector<STAI_Spawn>>& spawns;
            bool operator()(CreatureDataPair const& pair)
            {
                const CreatureData& data = pair.second;
                if (data.id)    // 0: the entry comes from a spawn group
                    spawns[data.mapid].push_back({ data.id, data.posX, data.posY, data.posZ });
                return false;
            }
        } worker{ spawns };
        sObjectMgr.DoCreatureData(worker);
    });

    static const std::vector<STAI_Spawn> none;
    auto found = spawns.find(mapId);
    return found == spawns.end() ? none : found->second;
}
