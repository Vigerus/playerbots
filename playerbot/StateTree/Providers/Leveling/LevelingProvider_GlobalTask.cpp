#include "LevelingProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Providers/Leveling/HuntProvider_GlobalTask.h"
#include "playerbot/StateTree/Providers/Leveling/STAI_SpawnIndex.h"

#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"
#include "Maps/GridMap.h"
#include "Server/DBCStores.h"
#include "Util/Timer.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 5000;
    constexpr float atAreaRadius = 150.0f;                              // yards: the bot grinds at the area (HuntProvider's AreaRadius)
    constexpr uint32 judgeAfterMs = 5 * MINUTE * IN_MILLISECONDS;       // time at the area before its pay counts
    constexpr uint32 judgeWindowMs = 15 * MINUTE * IN_MILLISECONDS;     // the pay is measured again from then on
    constexpr uint32 leaveAreaMs = 20 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 leaveZoneMs = 30 * MINUTE * IN_MILLISECONDS;

    // what has to outlive the bot's trees (they start again after every fight); only the bot's own map thread touches
    // its entry, the lock guards the map itself (its nodes stay where they are)
    struct BotPlan
    {
        uint32 level = 0;
        uint32 zoneId = 0;
        bool hasArea = false;
        float areaX = 0.0f, areaY = 0.0f;

        // experience earned since the plan was made, across level ups
        uint32 xp = 0, nextLevelXp = 0, xpLevel = 0;
        uint64 earned = 0;

        // the pay at the area: time spent within atAreaRadius and what was earned since the measuring started
        uint32 lastCheckMs = 0;
        uint32 atAreaMs = 0;
        uint64 earnedAtStart = 0;

        std::vector<std::pair<std::pair<float, float>, uint32>> leftAreas;  // position, when
        std::vector<std::pair<uint32, uint32>> leftZones;                   // zone id, when
    };

    std::mutex plansLock;
    std::unordered_map<ObjectGuid, BotPlan> plans;

    BotPlan& GetPlan(ObjectGuid guid)
    {
        std::lock_guard<std::mutex> lock(plansLock);
        return plans[guid];
    }

    float ParseFloatSetting(const STAI_TaskConfig& config, const std::string& name, const std::string& attribute, float fallback)
    {
        const std::optional<std::string> text = STAI_TaskFactory::GetSetting(config, name, attribute);
        if (!text)
            return fallback;

        char* end = nullptr;
        const float value = std::strtof(text->c_str(), &end);
        if (end == text->c_str() || *end || value < 0.0f)
            throw std::runtime_error("<" + name + " " + attribute + "=\"" + *text + "\">: expected a number of 0 or more");
        return value;
    }

    float ZoneCenterDistance(Player* bot, uint32 zoneId)
    {
        float x = 50.0f, y = 50.0f;
        return Zone2MapCoordinates(x, y, zoneId) ? bot->GetDistance2d(x, y) : 100000.0f;
    }
}

const std::vector<LevelingProvider_GlobalTask::Zone>& LevelingProvider_GlobalTask::GetZones()
{
    static const std::vector<Zone> zones =
    {
        // Alliance
        { 1,    0, "Dun Morogh",            ALLIANCE, 1,  10 },
        { 12,   0, "Elwynn Forest",         ALLIANCE, 1,  10 },
        { 141,  1, "Teldrassil",            ALLIANCE, 1,  10 },
        { 148,  1, "Darkshore",             ALLIANCE, 10, 20 },
        { 38,   0, "Loch Modan",            ALLIANCE, 10, 20 },
        { 40,   0, "Westfall",              ALLIANCE, 10, 20 },
        // Horde
        { 14,   1, "Durotar",               HORDE,    1,  10 },
        { 215,  1, "Mulgore",               HORDE,    1,  10 },
        { 85,   0, "Tirisfal Glades",       HORDE,    1,  10 },
        { 130,  0, "Silverpine Forest",     HORDE,    10, 20 },
        { 17,   1, "The Barrens",           HORDE,    10, 25 },
        // contested
        { 44,   0, "Redridge Mountains",    0,        15, 27 },
        { 406,  1, "Stonetalon Mountains",  0,        15, 27 },
        { 331,  1, "Ashenvale",             0,        18, 30 },
        { 10,   0, "Duskwood",              0,        18, 30 },
        { 267,  0, "Hillsbrad Foothills",   0,        20, 30 },
        { 11,   0, "Wetlands",              0,        20, 30 },
        { 400,  1, "Thousand Needles",      0,        25, 35 },
        { 36,   0, "Alterac Mountains",     0,        30, 40 },
        { 45,   0, "Arathi Highlands",      0,        30, 40 },
        { 405,  1, "Desolace",              0,        30, 40 },
        { 33,   0, "Stranglethorn Vale",    0,        30, 45 },
        { 15,   1, "Dustwallow Marsh",      0,        35, 45 },
        { 3,    0, "Badlands",              0,        35, 45 },
        { 8,    0, "Swamp of Sorrows",      0,        35, 45 },
        { 357,  1, "Feralas",               0,        40, 50 },
        { 47,   0, "The Hinterlands",       0,        40, 50 },
        { 440,  1, "Tanaris",               0,        40, 50 },
        { 51,   0, "Searing Gorge",         0,        45, 50 },
        { 16,   1, "Azshara",               0,        45, 55 },
        { 4,    0, "Blasted Lands",         0,        45, 55 },
        { 490,  1, "Un'Goro Crater",        0,        48, 55 },
        { 361,  1, "Felwood",               0,        48, 55 },
        { 46,   0, "Burning Steppes",       0,        50, 58 },
        { 28,   0, "Western Plaguelands",   0,        51, 58 },
        { 139,  0, "Eastern Plaguelands",   0,        53, 60 },
        { 618,  1, "Winterspring",          0,        53, 60 },
        { 41,   0, "Deadwind Pass",         0,        55, 60 },
        { 493,  1, "Moonglade",             0,        55, 60 },
        { 1377, 1, "Silithus",              0,        55, 60 },
    };
    return zones;
}

LevelingProvider_GlobalTask::Settings LevelingProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    settings.levelsBelow = uint32(ParseFloatSetting(config, "Levels", "below", float(settings.levelsBelow)));
    settings.levelsAbove = uint32(ParseFloatSetting(config, "Levels", "above", float(settings.levelsAbove)));
    settings.cell = ParseFloatSetting(config, "Cell", "value", settings.cell);
    settings.minCount = uint32(ParseFloatSetting(config, "MinCount", "value", float(settings.minCount)));
    settings.maxHours = ParseFloatSetting(config, "MaxHours", "value", settings.maxHours);
    if (settings.cell < 10.0f)
        throw std::runtime_error("<Cell value>: expected 10 yards or more");
    if (settings.maxHours <= 0.0f)
        throw std::runtime_error("<MaxHours value>: expected more than 0");
    return settings;
}

void LevelingProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_TaskBase> LevelingProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<LevelingProvider_GlobalTask>(ParseSettings(config));
}

const LevelingProvider_GlobalTask::Zone* LevelingProvider_GlobalTask::ChooseZone(Player* bot, uint32 preferred, const std::vector<uint32>& left) const
{
    const uint32 level = bot->GetLevel();
    const uint32 team = bot->GetTeam();
    // TODO: other continents (boats, zeppelins: travel across maps)
    auto usable = [&](const Zone& zone)
    {
        return zone.mapId == bot->GetMapId() && (!zone.team || zone.team == team) &&
            std::find(left.begin(), left.end(), zone.zoneId) == left.end();
    };
    auto fits = [&](const Zone& zone) { return usable(zone) && level >= zone.minLevel && level <= zone.maxLevel; };

    // the planned zone, else the bot's own, while they fit: no hopping while walking through other zones
    for (const uint32 zoneId : { preferred, bot->GetZoneId() })
        for (const Zone& zone : GetZones())
            if (zone.zoneId == zoneId && fits(zone))
                return &zone;

    // the range centered nearest the bot's level (half levels apart count the same), then the nearest zone; without a
    // fitting one the nearest range
    const Zone* best = nullptr;
    float bestLevels = 0.0f, bestDistance = 0.0f;
    for (const Zone& zone : GetZones())
    {
        if (!usable(zone))
            continue;

        const float levels = fits(zone) ? std::round(std::abs(float(level) - (zone.minLevel + zone.maxLevel) / 2.0f) * 2.0f) / 2.0f :
            100.0f + (level < zone.minLevel ? zone.minLevel - level : level - zone.maxLevel);
        const float distance = ZoneCenterDistance(bot, zone.zoneId);
        if (!best || levels < bestLevels || (levels == bestLevels && distance < bestDistance))
        {
            best = &zone;
            bestLevels = levels;
            bestDistance = distance;
        }
    }
    return best;
}

const std::vector<LevelingProvider_GlobalTask::Area>& LevelingProvider_GlobalTask::GetAreas(Player* bot, const Zone& zone) const
{
    const uint32 level = bot->GetLevel();
    const FactionTemplateEntry* botFaction = bot->GetFactionTemplateEntry();
    const uint64 key = (uint64(zone.zoneId) << 32) | (uint64(bot->GetFaction()) << 8) | level;

    std::lock_guard<std::mutex> lock(areasLock);
    auto known = areasCache.find(key);
    if (known != areasCache.end())
        return known->second;

    struct Cell
    {
        uint32 count = 0;
        float levelSum = 0.0f, sumX = 0.0f, sumY = 0.0f;
        bool danger = false;
        std::vector<const STAI_Spawn*> spawns;
    };
    std::unordered_map<int64, Cell> cells;
    auto cellKey = [](int32 cx, int32 cy) { return (int64(cx) << 32) | uint32(cy); };

    for (const STAI_Spawn& spawn : STAI_GetSpawns(zone.mapId))
    {
        if (!MapCoordinateVsZoneCheck(spawn.x, spawn.y, zone.mapId, zone.zoneId))
            continue;
        const CreatureInfo* info = ObjectMgr::GetCreatureTemplate(spawn.entry);
        if (!info)
            continue;

        Cell& cell = cells[cellKey(int32(std::floor(spawn.x / settings.cell)), int32(std::floor(spawn.y / settings.cell)))];
        if (HuntProvider_GlobalTask::IsHuntable(botFaction, level, info, settings.levelsBelow, settings.levelsAbove, false))
        {
            ++cell.count;
            cell.levelSum += (info->MinLevel + info->MaxLevel) / 2.0f;
            cell.sumX += spawn.x;
            cell.sumY += spawn.y;
            cell.spawns.push_back(&spawn);
            continue;
        }

        // elites (until groups grind) and creatures well above the bot make the place unsafe
        const FactionTemplateEntry* faction = sFactionTemplateStore.LookupEntry(info->Faction);
        const bool hostile = faction && botFaction && !faction->IsFriendlyTo(*botFaction) && !info->NpcFlags && !info->Civilian &&
            info->CreatureType != CREATURE_TYPE_CRITTER;
        if (hostile && (info->Rank != CREATURE_ELITE_NORMAL || info->MinLevel > level + settings.levelsAbove + 2))
            cell.danger = true;
    }

    std::vector<Area>& areas = areasCache[key];
    TerrainInfo const* terrain = sTerrainMgr.LoadTerrain(zone.mapId);
    for (const auto& [ckey, cell] : cells)
    {
        if (cell.count < settings.minCount)
            continue;

        const int32 cx = int32(ckey >> 32), cy = int32(uint32(ckey));
        bool danger = false;
        for (int32 dx = -1; dx <= 1 && !danger; ++dx)
            for (int32 dy = -1; dy <= 1 && !danger; ++dy)
            {
                auto neighbour = cells.find(cellKey(cx + dx, cy + dy));
                danger = neighbour != cells.end() && neighbour->second.danger;
            }
        if (danger)
            continue;

        // the spawn nearest the middle: a place the creatures stand on
        const float mx = cell.sumX / cell.count, my = cell.sumY / cell.count;
        const STAI_Spawn* middle = *std::min_element(cell.spawns.begin(), cell.spawns.end(), [mx, my](const STAI_Spawn* a, const STAI_Spawn* b)
        {
            return (a->x - mx) * (a->x - mx) + (a->y - my) * (a->y - my) < (b->x - mx) * (b->x - mx) + (b->y - my) * (b->y - my);
        });
        // the zone rectangles overlap their neighbours
        if (terrain && terrain->GetZoneId(middle->x, middle->y, middle->z) != zone.zoneId)
            continue;

        areas.push_back({ middle->x, middle->y, middle->z, cell.count, cell.levelSum / cell.count });
    }
    return areas;
}

void LevelingProvider_GlobalTask::Update(LevelingProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    if (!bot->IsInWorld())
        return;

    const uint32 now = WorldTimer::getMSTime();
    const uint32 level = bot->GetLevel();
    BotPlan& plan = GetPlan(bot->GetObjectGuid());

    plan.leftAreas.erase(std::remove_if(plan.leftAreas.begin(), plan.leftAreas.end(),
        [now](const auto& left) { return now - left.second >= leaveAreaMs; }), plan.leftAreas.end());
    plan.leftZones.erase(std::remove_if(plan.leftZones.begin(), plan.leftZones.end(),
        [now](const auto& left) { return now - left.second >= leaveZoneMs; }), plan.leftZones.end());

    // experience, across level ups
    const uint32 xp = bot->GetUInt32Value(PLAYER_XP);
    if (plan.xpLevel && level == plan.xpLevel && xp >= plan.xp)
        plan.earned += xp - plan.xp;
    else if (plan.xpLevel && level > plan.xpLevel)
        plan.earned += (plan.nextLevelXp > plan.xp ? plan.nextLevelXp - plan.xp : 0) + xp;
    plan.xp = xp;
    plan.xpLevel = level;
    plan.nextLevelXp = bot->GetUInt32Value(PLAYER_NEXT_LEVEL_XP);

    // a level up: other creatures fit, the groups are chosen again
    if (plan.level != level)
    {
        plan.level = level;
        plan.hasArea = false;
    }

    // the pay at the area
    const uint32 sinceCheck = plan.lastCheckMs ? now - plan.lastCheckMs : 0;
    plan.lastCheckMs = now;
    context.xpPerHour = 0;
    if (plan.hasArea && bot->GetDistance2d(plan.areaX, plan.areaY) <= atAreaRadius)
    {
        if (!plan.atAreaMs)
            plan.earnedAtStart = plan.earned;
        plan.atAreaMs += std::max<uint32>(sinceCheck, 1);
    }
    if (plan.hasArea && plan.atAreaMs >= judgeAfterMs)
    {
        const float hours = plan.atAreaMs / float(HOUR * IN_MILLISECONDS);
        context.xpPerHour = uint32((plan.earned - plan.earnedAtStart) / hours);
        if (context.xpPerHour < plan.nextLevelXp / settings.maxHours)
        {
            plan.leftAreas.push_back({ { plan.areaX, plan.areaY }, now });
            plan.hasArea = false;
        }
        else if (plan.atAreaMs >= judgeWindowMs)
            plan.atAreaMs = 0;
    }

    // the zone, and a group in it; a zone without a group left is left too
    const Zone* zone = nullptr;
    const std::vector<Area>* areas = nullptr;
    const Area* area = nullptr;
    for (int attempt = 0; attempt < 3 && !area; ++attempt)
    {
        std::vector<uint32> leftZones;
        for (const auto& left : plan.leftZones)
            leftZones.push_back(left.first);
        zone = ChooseZone(bot, plan.zoneId, leftZones);
        if (!zone)
            break;
        if (zone->zoneId != plan.zoneId)
        {
            plan.zoneId = zone->zoneId;
            plan.hasArea = false;
        }

        areas = &GetAreas(bot, *zone);
        auto isLeft = [&plan](const Area& a)
        {
            return std::any_of(plan.leftAreas.begin(), plan.leftAreas.end(), [&a](const auto& left)
            {
                return std::abs(left.first.first - a.x) < 1.0f && std::abs(left.first.second - a.y) < 1.0f;
            });
        };

        if (plan.hasArea)
        {
            for (const Area& a : *areas)
                if (std::abs(a.x - plan.areaX) < 1.0f && std::abs(a.y - plan.areaY) < 1.0f)
                    area = &a;
            if (!area)
                plan.hasArea = false;
        }
        if (!plan.hasArea)
        {
            // the average level nearest the bot's (half levels apart count the same), then the nearest
            float bestLevels = 0.0f, bestDistance = 0.0f;
            for (const Area& a : *areas)
            {
                if (isLeft(a))
                    continue;
                const float levels = std::round(std::abs(a.avgLevel - level) * 2.0f) / 2.0f;
                const float distance = bot->GetDistance2d(a.x, a.y);
                if (!area || levels < bestLevels || (levels == bestLevels && distance < bestDistance))
                {
                    area = &a;
                    bestLevels = levels;
                    bestDistance = distance;
                }
            }
        }

        if (!area)
            plan.leftZones.push_back({ zone->zoneId, now });
    }

    if (area && (!plan.hasArea || area->x != plan.areaX || area->y != plan.areaY))
    {
        plan.hasArea = true;
        plan.areaX = area->x;
        plan.areaY = area->y;
        plan.atAreaMs = 0;
    }
    if (!area)
        plan.hasArea = false;

    const uint32 previousZone = context.zoneId;
    const STAI_Location previousArea = context.huntArea;
    context.zoneId = zone && area ? zone->zoneId : 0;
    context.zoneName = zone && area ? zone->name : "";
    context.inZone = context.zoneId && bot->GetZoneId() == context.zoneId;
    context.huntArea = area ? STAI_Location{ zone->mapId, area->x, area->y, area->z, true } : STAI_Location();
    context.areaLevel = area ? uint32(std::round(area->avgLevel)) : 0;
    context.areas = areas ? uint32(areas->size()) : 0;

    if (context.zoneId != previousZone)
        context.onZoneChanged.Broadcast();
    if (context.huntArea.x != previousArea.x || context.huntArea.y != previousArea.y || context.huntArea.IsEmpty() != previousArea.IsEmpty())
        context.onAreaChanged.Broadcast();
}

EActionRunStatus LevelingProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LevelingProvider_GlobalTask::OnEnterState);
    LevelingProvider_GlobalTaskContext& context = ContextAs<LevelingProvider_GlobalTaskContext>(baseContext);
    context.zoneId = 0;
    context.zoneName.clear();
    context.inZone = false;
    context.huntArea = STAI_Location();
    context.areaLevel = context.areas = context.xpPerHour = 0;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus LevelingProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LevelingProvider_GlobalTask::OnTick);
    LevelingProvider_GlobalTaskContext& context = ContextAs<LevelingProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& LevelingProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("zoneId", &LevelingProvider_GlobalTaskContext::zoneId),
        STAI_PropertyDef::Of("zoneName", &LevelingProvider_GlobalTaskContext::zoneName),
        STAI_PropertyDef::Of("inZone", &LevelingProvider_GlobalTaskContext::inZone),
        STAI_PropertyDef::Of("huntArea", &LevelingProvider_GlobalTaskContext::huntArea),
        STAI_PropertyDef::Of("areaLevel", &LevelingProvider_GlobalTaskContext::areaLevel),
        STAI_PropertyDef::Of("areas", &LevelingProvider_GlobalTaskContext::areas),
        STAI_PropertyDef::Of("xpPerHour", &LevelingProvider_GlobalTaskContext::xpPerHour),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& LevelingProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onZoneChanged", &LevelingProvider_GlobalTaskContext::onZoneChanged),
        STAI_DelegateDef::Of("onAreaChanged", &LevelingProvider_GlobalTaskContext::onAreaChanged),
    };
    return delegates;
}
