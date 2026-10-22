#include "playerbot/TravelMgr.h"
#include <numeric>
#include <iomanip>

#include "MotionGenerators/PathFinder.h"
#include "TravelNode.h"
#include "PlayerbotAI.h"
#include "Globals/ObjectAccessor.h"

using namespace ai;
using namespace MaNGOS;

int32 TravelMgr::GetAreaLevel(uint32 area_id)
{
    auto lev = areaLevels.find(area_id);

    if (lev != areaLevels.end())
        return lev->second;

    AreaTableEntry const* area = GetAreaEntryByAreaID(area_id);

    if (!area)
    {
        areaLevels[area_id] = -2;
        return -2;
    }

    //Get exploration level
    if (area->area_level) 
    {
        areaLevels[area_id] = area->area_level;
        return area->area_level;
    }


    int32 level = 0;
    uint32 cnt = 0;

    //Get sub-area's
    for (uint32 i = 0; i <= sAreaStore.GetNumRows(); i++)
    {
        AreaTableEntry const* subArea = GetAreaEntryByAreaID(i);

        if (!subArea || subArea->zone != area->ID)
            continue;

        int32 subLevel = GetAreaLevel(subArea->ID);

        if (!subLevel)
            continue;

        level += subLevel;

        cnt++;
    }

    if (cnt)
    {
        areaLevels[area_id] = std::max(uint32(1), level / cnt);
        return areaLevels[area_id];
    }

    //Get units avarage
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    for (auto& creaturePair : WorldPosition().getCreaturesNear())
    {
        if (WorldPosition(creaturePair).GetArea() != area)
            continue;

        CreatureData const cData = creaturePair->second;
        CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(cData.id);

        if (!cInfo)
            continue;

        FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry);

        if (reactionHum > REP_NEUTRAL || reactionOrc > REP_NEUTRAL)
            continue;

        level += cInfo->MaxLevel;
        cnt++;
    }

    if (cnt)
    {
        areaLevels[area_id] = std::max(uint32(1),level / cnt);
        return areaLevels[area_id];
    }

    //Use parent zone value.
    if (area->zone)
    {
        areaLevels[area_id] = 0; //Set a temporary value so it wont be counted.
        level = GetAreaLevel(area->zone);
        areaLevels[area_id] = level;        
        return areaLevels[area_id];
    }

    areaLevels[area_id] = -1;

    return areaLevels[area_id];
}

void TravelMgr::LoadAreaLevels()
{
    if (!areaLevels.empty())
        return;

    WorldDatabase.PExecute("CREATE TABLE IF NOT EXISTS `ai_playerbot_zone_level` (`id` bigint(20) NOT NULL ,`level` bigint(20) NOT NULL,PRIMARY KEY(`id`))");

    std::string query = "SELECT id, level FROM ai_playerbot_zone_level";

    {
        auto result = WorldDatabase.PQuery("%s", query.c_str());

        std::vector<uint32> loadedAreas;

        if (result)
        {
            BarGoLink bar(result->GetRowCount());            

            do
            {
                Field* fields = result->Fetch();
                bar.step();

                areaLevels[fields[0].GetUInt32()] = fields[1].GetInt32();

                loadedAreas.push_back(fields[0].GetUInt32());
            } while (result->NextRow());

            sLog.outString(">> Loaded " SIZEFMTD " area levels.", areaLevels.size());
        }

        BarGoLink bar(sAreaStore.GetNumRows());
        WorldDatabase.BeginTransaction();
        for (uint32 i = 0; i < sAreaStore.GetNumRows(); ++i)    // areaflag numbered from 0
        {
            bar.step();
            if (AreaTableEntry const* area = sAreaStore.LookupEntry(i))
            {
                if (std::find(loadedAreas.begin(), loadedAreas.end(), area->ID) == loadedAreas.end())
                {
                    int32 level = sTravelMgr.GetAreaLevel(area->ID);

                    WorldDatabase.PExecute("INSERT INTO `ai_playerbot_zone_level` (`id`, `level`) VALUES ('%d', '%d')", area->ID, level);
                }
            }
        }
        WorldDatabase.CommitTransaction();
        if(areaLevels.size() > loadedAreas.size())
            sLog.outString(">> Generated " SIZEFMTD " areas.", areaLevels.size()- loadedAreas.size());
    }
}

void TravelMgr::SetMobAvoidArea()
{
    sLog.outString("-Apply mob avoidance maps");

    std::vector<std::future<void>> calculations;

    BarGoLink bar(sMapStore.GetNumRows());

    for (uint32 i = 0; i < sMapStore.GetNumRows(); ++i)
    {
        if (!sMapStore.LookupEntry(i))
            continue;
        
        uint32 mapId = sMapStore.LookupEntry(i)->MapID;
        calculations.push_back(std::async([this, mapId] { SetMobAvoidAreaMap(mapId); }));
        bar.step();
    }

    BarGoLink bar2(calculations.size());
    for (uint32 i = 0; i < calculations.size(); i++)
    {
        calculations[i].wait();
        bar2.step();
    }

    sLog.outString(">> Modified navmap areas for %d maps.", sMapStore.GetNumRows());
}

void TravelMgr::SetMobAvoidAreaMap(uint32 mapId) 
{
    PathFinder path(mapId, 0);
    FactionTemplateEntry const* humanFaction = sFactionTemplateStore.LookupEntry(1);
    FactionTemplateEntry const* orcFaction = sFactionTemplateStore.LookupEntry(2);

    std::vector<CreatureDataPair const*> creatures = WorldPosition(mapId, 1,1).getCreaturesNear();

    for (auto& creaturePair : creatures)
    {
        CreatureData const cData = creaturePair->second;
        CreatureInfo const* cInfo = ObjectMgr::GetCreatureTemplate(cData.id);

        if (!cInfo)
            continue;

        WorldPosition point = WorldPosition(cData.mapid, cData.posX, cData.posY, cData.posZ, cData.orientation);

        if (cInfo->NpcFlags > 0)
            continue;

        FactionTemplateEntry const* factionEntry = sFactionTemplateStore.LookupEntry(cInfo->Faction);
        ReputationRank reactionHum = PlayerbotAI::GetFactionReaction(humanFaction, factionEntry);
        ReputationRank reactionOrc = PlayerbotAI::GetFactionReaction(orcFaction, factionEntry);

        if (reactionHum >= REP_NEUTRAL || reactionOrc >= REP_NEUTRAL)
            continue;

        if (!point.getTerrain())
            continue;

        if (!point.loadMapAndVMap(0))
            continue;

        path.setArea(point.getMapId(), point.getX(), point.getY(), point.getZ(), 12, 50.0f);
        path.setArea(point.getMapId(), point.getX(), point.getY(), point.getZ(), 13, 20.0f);
    }
}

void TravelMgr::AddMapTransfer(WorldPosition start, WorldPosition end, float portalDistance, bool makeShortcuts)
{
    uint32 sMap = start.getMapId();
    uint32 eMap = end.getMapId();

    if (sMap == eMap)
        return;
    
    //Calculate shortcuts.
    if(makeShortcuts)
        for (auto& mapTransfers : mapTransfersMap)
        {
            uint32 sMapt = mapTransfers.first.first;
            uint32 eMapt = mapTransfers.first.second;

            for (auto& mapTransfer : mapTransfers.second)
            {
                if (eMapt == sMap && sMapt != eMap) // [S1 >MT> E1 -> S2] >THIS> E2
                {
                    float newDistToEnd = MapTransDistance(mapTransfer.GetPointFrom(), start) + portalDistance;
                    if (MapTransDistance(mapTransfer.GetPointFrom(), end) > newDistToEnd)
                        AddMapTransfer(mapTransfer.GetPointFrom(), end, newDistToEnd, false);
                }

                if (sMapt == eMap && eMapt != sMap) // S1 >THIS> [E1 -> S2 >MT> E2]
                {
                    float newDistToEnd = portalDistance + MapTransDistance(end, mapTransfer.GetPointTo());
                    if (MapTransDistance(start, mapTransfer.GetPointTo()) > newDistToEnd)
                        AddMapTransfer(start, mapTransfer.GetPointTo(), newDistToEnd, false);
                }
            }
        }

    //Add actual transfer.
    auto mapTransfers = mapTransfersMap.find(std::make_pair(start.getMapId(), end.getMapId()));
    
    if (mapTransfers == mapTransfersMap.end())
        mapTransfersMap.insert({ { sMap, eMap }, {MapTransfer(start, end, portalDistance)} });
    else
        mapTransfers->second.push_back(MapTransfer(start, end, portalDistance));        
};

void TravelMgr::LoadMapTransfers()
{
    for (auto& startNode : sTravelNodeMap.getNodes())
    {
        for (auto& [endNode, path] : *startNode->getLinks())
        {
            AddMapTransfer(*startNode->getPosition(), *endNode->getPosition(), path->getDistance());
        }
    }
}

std::vector<std::pair<WorldPosition, float>> TravelMgr::sqMapTransDistances(const WorldPosition& start, uint32 endMapId) const
{
    uint32 startMapId = start.getMapId();
    if (startMapId == endMapId)
        return { { start, 0.0f } };

    auto mapTransfers = mapTransfersMap.find({ startMapId, endMapId });   

    if (mapTransfers == mapTransfersMap.end())
        return {};

    std::vector<std::pair<WorldPosition, float>> retPortals;

    for (auto& mapTrans : mapTransfers->second)
    {
        WorldPosition portalOnEndMap = mapTrans.GetPointTo();

        float sqDist = mapTrans.sqDist(start, portalOnEndMap);

        retPortals.push_back(std::make_pair(portalOnEndMap, sqDist));
    }

    return retPortals;
}

float TravelMgr::MapTransDistance(const WorldPosition& start, const WorldPosition& end, bool toMap) const
{
    //These are the portals on the end map that start can reach.
    std::vector<std::pair<WorldPosition, float>> portals = sqMapTransDistances(start, end.getMapId());

    if (portals.empty())
        return FLT_MAX;

    float minsqDist = FLT_MAX;

    for (auto& [portal, sqDistanceFromStartToPortal] : portals)
    {
        float sqDist = sqDistanceFromStartToPortal + portal.sqDistance2d(end);
        if (sqDist < minsqDist)
            minsqDist = sqDist;
    }

    return sqrt(minsqDist);
}