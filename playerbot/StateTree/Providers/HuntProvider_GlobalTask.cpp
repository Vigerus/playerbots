#include "HuntProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/Creature.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 500;
    constexpr uint32 huntCheckIntervalMs = 5000;
    constexpr float reachedHuntLocation = 20.0f;    // yards: the hunt location counts as visited
    constexpr float visitedRadius = 40.0f;          // spawn points this close to a visited one are skipped
    constexpr uint32 visitedForgetMs = 10 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 keepTargetMs = 30 * IN_MILLISECONDS;          // a unit not pulled by then is unreachable
    constexpr uint32 ignoreTargetMs = 5 * MINUTE * IN_MILLISECONDS;

    struct SpawnPoint
    {
        uint32 entry;
        float x, y, z;
    };

    // every creature spawn by map, built once on first use (read only afterwards, so any map thread may read it)
    const std::unordered_map<uint32, std::vector<SpawnPoint>>& GetSpawnsByMap()
    {
        static std::unordered_map<uint32, std::vector<SpawnPoint>> spawns;
        static std::once_flag built;
        std::call_once(built, []()
        {
            struct Worker
            {
                std::unordered_map<uint32, std::vector<SpawnPoint>>& spawns;
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
        return spawns;
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
}

HuntProvider_GlobalTask::Settings HuntProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    settings.range = ParseFloatSetting(config, "Range", "value", settings.range);
    settings.levelsBelow = uint32(ParseFloatSetting(config, "Levels", "below", float(settings.levelsBelow)));
    settings.levelsAbove = uint32(ParseFloatSetting(config, "Levels", "above", float(settings.levelsAbove)));
    settings.safeRadius = ParseFloatSetting(config, "SafeRadius", "value", settings.safeRadius);
    settings.huntRange = ParseFloatSetting(config, "HuntRange", "value", settings.huntRange);

    const std::string elite = STAI_TaskFactory::GetSetting(config, "Elite").value_or("false");
    if (elite != "true" && elite != "false")
        throw std::runtime_error("<Elite value=\"" + elite + "\">: expected true or false");
    settings.elite = elite == "true";

    if (settings.range <= 0.0f)
        throw std::runtime_error("Range must be greater than 0");
    return settings;
}

void HuntProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_TaskBase> HuntProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<HuntProvider_GlobalTask>(ParseSettings(config));
}

bool HuntProvider_GlobalTask::FitsLevel(const Player* bot, uint32 minLevel, uint32 maxLevel) const
{
    const uint32 level = bot->GetLevel();
    const uint32 lowest = level > settings.levelsBelow ? level - settings.levelsBelow : 1;
    return maxLevel >= lowest && minLevel <= level + settings.levelsAbove;
}

bool HuntProvider_GlobalTask::CanHunt(Player* bot, Creature* creature) const
{
    if (!creature->IsInWorld() || !sServerFacade.IsAlive(creature) || creature->IsInCombat())
        return false;

    // pets, totems and guardians belong to somebody; critters, civilians and npcs with a service are left alone
    if (!creature->GetOwnerGuid().IsEmpty() || creature->IsCritter() || creature->IsCivilian() || creature->GetUInt32Value(UNIT_NPC_FLAGS))
        return false;

    if (!settings.elite && creature->GetCreatureInfo()->Rank != CREATURE_ELITE_NORMAL)
        return false;

    if (!FitsLevel(bot, creature->GetLevel(), creature->GetLevel()))
        return false;

    // tapped by another player or group
    if (creature->HasLootRecipient() && !creature->IsTappedBy(bot))
        return false;

    return bot->CanAttack(creature) && !bot->IsFriend(creature);
}

bool HuntProvider_GlobalTask::IsSafeToPull(Player* bot, Creature* creature) const
{
    CreatureList around;
    MaNGOS::AnyUnitInObjectRangeCheck check(creature, settings.safeRadius);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(around, check);
    Cell::VisitGridObjects(creature, searcher, settings.safeRadius);

    // a neutral creature does not join; an aggressive one that is not fighting yet would
    for (Creature* other : around)
        if (other != creature && !other->IsInCombat() && other->GetOwnerGuid().IsEmpty() && other->IsEnemy(bot))
            return false;
    return true;
}

void HuntProvider_GlobalTask::UpdateTarget(HuntProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const ObjectGuid previous = context.pullTarget;
    context.pullTarget = ObjectGuid();
    context.candidates = 0;

    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    context.ignored.erase(std::remove_if(context.ignored.begin(), context.ignored.end(),
        [&context](const std::pair<ObjectGuid, uint32>& entry) { return context.nowMs >= entry.second; }), context.ignored.end());
    auto isIgnored = [&context](const ObjectGuid& guid)
    {
        return std::any_of(context.ignored.begin(), context.ignored.end(), [&guid](const std::pair<ObjectGuid, uint32>& entry) { return entry.first == guid; });
    };

    if (!previous.IsEmpty() && context.targetSinceMs >= keepTargetMs)
    {
        context.ignored.push_back({ previous, context.nowMs + ignoreTargetMs });
        context.targetSinceMs = 0;
    }

    // the chosen unit is kept while it can still be hunted, so the bot does not switch on the way
    if (!previous.IsEmpty() && !isIgnored(previous))
    {
        Creature* kept = bot->GetMap()->GetCreature(previous);
        if (kept && CanHunt(bot, kept) && bot->IsWithinDistInMap(kept, settings.range))
        {
            context.pullTarget = previous;
            context.candidates = 1;
            return;
        }
    }

    CreatureList found;
    MaNGOS::AnyUnitInObjectRangeCheck check(bot, settings.range);
    MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, settings.range);

    std::vector<Creature*> candidates;
    for (Creature* creature : found)
        if (CanHunt(bot, creature) && !isIgnored(creature->GetObjectGuid()))
            candidates.push_back(creature);
    context.candidates = uint32(candidates.size());

    std::sort(candidates.begin(), candidates.end(), [bot](Creature* a, Creature* b) { return bot->GetDistance(a) < bot->GetDistance(b); });
    for (Creature* creature : candidates)
    {
        if (bot->IsWithinLOSInMap(creature) && IsSafeToPull(bot, creature))
        {
            context.pullTarget = creature->GetObjectGuid();
            break;
        }
    }

    if (context.pullTarget != previous)
        context.targetSinceMs = 0;
    if (!context.pullTarget.IsEmpty() && context.pullTarget != previous)
        context.onPullTarget.Broadcast();
}

void HuntProvider_GlobalTask::UpdateHuntLocation(HuntProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;

    // reached: remembered as visited, the next one is chosen
    if (!context.huntLocation.IsEmpty() && context.huntLocation.mapId == bot->GetMapId() &&
        bot->GetDistance2d(context.huntLocation.x, context.huntLocation.y) < reachedHuntLocation)
        context.visited.push_back({ context.huntLocation.x, context.huntLocation.y, context.nowMs });

    context.visited.erase(std::remove_if(context.visited.begin(), context.visited.end(),
        [&context](const HuntProvider_GlobalTaskContext::Visited& v) { return context.nowMs - v.atMs > visitedForgetMs; }), context.visited.end());

    context.huntLocation = STAI_Location();
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    const auto& spawnsByMap = GetSpawnsByMap();
    auto spawns = spawnsByMap.find(bot->GetMapId());
    if (spawns == spawnsByMap.end())
        return;

    const FactionTemplateEntry* botFaction = bot->GetFactionTemplateEntry();
    std::unordered_map<uint32, bool> huntable;     // by entry, so every template is checked once
    auto isHuntable = [&](uint32 entry)
    {
        auto known = huntable.find(entry);
        if (known != huntable.end())
            return known->second;

        bool result = false;
        if (const CreatureInfo* info = ObjectMgr::GetCreatureTemplate(entry))
        {
            const FactionTemplateEntry* faction = sFactionTemplateStore.LookupEntry(info->Faction);
            result = FitsLevel(bot, info->MinLevel, info->MaxLevel) && (settings.elite || info->Rank == CREATURE_ELITE_NORMAL) &&
                info->CreatureType != CREATURE_TYPE_CRITTER && !info->NpcFlags && !info->Civilian &&
                faction && botFaction && !faction->IsFriendlyTo(*botFaction);
        }
        huntable[entry] = result;
        return result;
    };

    const float minDistance = settings.range * 0.8f;   // closer ones would have been found as units
    float best = settings.huntRange;
    for (const SpawnPoint& spawn : spawns->second)
    {
        const float distance = bot->GetDistance2d(spawn.x, spawn.y);
        if (distance < minDistance || distance >= best || !isHuntable(spawn.entry))
            continue;

        const bool wasVisited = std::any_of(context.visited.begin(), context.visited.end(), [&spawn](const HuntProvider_GlobalTaskContext::Visited& v)
        {
            return (v.x - spawn.x) * (v.x - spawn.x) + (v.y - spawn.y) * (v.y - spawn.y) < visitedRadius * visitedRadius;
        });
        if (wasVisited)
            continue;

        best = distance;
        context.huntLocation = { bot->GetMapId(), spawn.x, spawn.y, spawn.z, true };
    }
}

EActionRunStatus HuntProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HuntProvider_GlobalTask::OnEnterState);
    HuntProvider_GlobalTaskContext& context = ContextAs<HuntProvider_GlobalTaskContext>(baseContext);
    context.pullTarget = ObjectGuid();
    context.candidates = 0;
    context.huntLocation = STAI_Location();
    context.sinceCheckMs = 0;
    context.sinceHuntCheckMs = 0;
    context.targetSinceMs = 0;
    UpdateTarget(context);
    if (context.pullTarget.IsEmpty())
        UpdateHuntLocation(context);
    return EActionRunStatus::Running;
}

EActionRunStatus HuntProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HuntProvider_GlobalTask::OnTick);
    HuntProvider_GlobalTaskContext& context = ContextAs<HuntProvider_GlobalTaskContext>(baseContext);
    context.nowMs += elapsed;
    context.sinceCheckMs += elapsed;
    if (!context.pullTarget.IsEmpty())
        context.targetSinceMs += elapsed;
    context.sinceHuntCheckMs += elapsed;

    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        UpdateTarget(context);
    }

    // only needed while there is nothing to pull; at once when the bot got there
    if (!context.pullTarget.IsEmpty())
        context.huntLocation = STAI_Location();
    else
    {
        const bool reached = !context.huntLocation.IsEmpty() && context.huntLocation.mapId == context.bot->GetMapId() &&
            context.bot->GetDistance2d(context.huntLocation.x, context.huntLocation.y) < reachedHuntLocation;
        if (reached || context.sinceHuntCheckMs >= huntCheckIntervalMs)
        {
            context.sinceHuntCheckMs = 0;
            UpdateHuntLocation(context);
        }
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& HuntProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("pullTarget", &HuntProvider_GlobalTaskContext::pullTarget),
        STAI_PropertyDef::Of("candidates", &HuntProvider_GlobalTaskContext::candidates),
        STAI_PropertyDef::Of("huntLocation", &HuntProvider_GlobalTaskContext::huntLocation),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& HuntProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onPullTarget", &HuntProvider_GlobalTaskContext::onPullTarget) };
    return delegates;
}
