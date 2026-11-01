#include "QuestProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Providers/Leveling/STAI_QuestIndex.h"

#include "Entities/Creature.h"
#include "Entities/GameObject.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Quests/QuestDef.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_map>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 1000;
    constexpr float nearNpc = 50.0f;                // yards: the giver / taker creature is looked up (for its guid)
    constexpr float stuckRadius = 30.0f;            // yards around a giver / taker spawn
    constexpr uint32 stuckMs = 30 * IN_MILLISECONDS;
    constexpr uint32 ignoreSpotMs = 10 * MINUTE * IN_MILLISECONDS;
    constexpr float reachedObjective = 20.0f;
    constexpr float visitedRadius = 40.0f;
    constexpr uint32 visitedForgetMs = 10 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 keepObjectMs = 30 * IN_MILLISECONDS;
    constexpr uint32 ignoreObjectMs = 5 * MINUTE * IN_MILLISECONDS;
    constexpr uint32 dropLevelsAbove = 3;           // a creature dropping a quest item may be this much above the bot

    // what an incomplete quest still needs: creatures to kill or loot, game objects to use or loot
    struct Needs
    {
        std::vector<uint32> creatures;
        std::vector<uint32> objects;

        bool Empty() const { return creatures.empty() && objects.empty(); }
    };

    struct Spawn
    {
        uint32 entry = 0;
        const STAI_SpawnPoint* spot = nullptr;
        float distance = std::numeric_limits<float>::max();
    };

    void AddUnique(std::vector<uint32>& entries, uint32 entry)
    {
        if (std::find(entries.begin(), entries.end(), entry) == entries.end())
            entries.push_back(entry);
    }

    float Distance2d(float x1, float y1, float x2, float y2)
    {
        return std::sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
    }

    bool IsNear(const std::vector<QuestProvider_GlobalTaskContext::Spot>& spots, uint32 entry, float x, float y, float radius)
    {
        return std::any_of(spots.begin(), spots.end(), [&](const QuestProvider_GlobalTaskContext::Spot& spot)
        {
            return (!entry || spot.entry == entry) && Distance2d(spot.x, spot.y, x, y) < radius;
        });
    }

    bool IsFriendlyNpc(const Player* bot, uint32 entry)
    {
        const CreatureInfo* info = ObjectMgr::GetCreatureTemplate(entry);
        const FactionTemplateEntry* faction = info ? sFactionTemplateStore.LookupEntry(info->Faction) : nullptr;
        const FactionTemplateEntry* botFaction = bot->GetFactionTemplateEntry();
        return faction && botFaction && !faction->IsHostileTo(*botFaction);
    }

    // what the quest still needs on the bot's map; false when an objective cannot be worked on (a spell to cast, an
    // area or event, a timer, or nothing on the map to get it from). status: nullptr for a quest not taken yet
    bool FindNeeds(Player* bot, const Quest* quest, const QuestStatusData* status, Needs& needs)
    {
        const STAI_QuestIndex& index = STAI_QuestIndex::Get();
        const uint32 mapId = bot->GetMapId();
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_TIMED))
            return false;
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT) && !(status && status->m_explored))
            return false;

        for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            const int32 id = quest->ReqCreatureOrGOId[i];
            if (!id)
                continue;
            if (quest->ReqSpell[i])
                return false;
            if (status && status->m_creatureOrGOcount[i] >= quest->ReqCreatureOrGOCount[i])
                continue;

            if (id > 0)
            {
                if (index.CreatureSpawns(mapId, uint32(id)).empty())
                    return false;
                AddUnique(needs.creatures, uint32(id));
            }
            else
            {
                if (index.ObjectSpawns(mapId, uint32(-id)).empty())
                    return false;
                AddUnique(needs.objects, uint32(-id));
            }
        }

        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
        {
            const uint32 itemId = quest->ReqItemId[i];
            if (!itemId || bot->GetItemCount(itemId) >= quest->ReqItemCount[i])
                continue;
            // a delivery: the giver hands the item over
            if (itemId == quest->GetSrcItemId() && !status)
                continue;

            bool found = false;
            for (uint32 entry : index.ItemCreatures(itemId))
            {
                const CreatureInfo* info = ObjectMgr::GetCreatureTemplate(entry);
                if (!info || info->Rank != CREATURE_ELITE_NORMAL || info->MinLevel > bot->GetLevel() + dropLevelsAbove)
                    continue;
                if (index.CreatureSpawns(mapId, entry).empty())
                    continue;
                AddUnique(needs.creatures, entry);
                found = true;
            }
            for (uint32 entry : index.ItemObjects(itemId))
            {
                if (index.ObjectSpawns(mapId, entry).empty())
                    continue;
                AddUnique(needs.objects, entry);
                found = true;
            }
            if (!found)
                return false;
        }
        return true;
    }

    // the creature of the entry nearest to the bot within nearNpc yards, alive
    ObjectGuid FindNpc(Player* bot, uint32 entry)
    {
        CreatureList found;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, entry, nearNpc);
        MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(found, check);
        Cell::VisitGridObjects(bot, searcher, nearNpc);

        Creature* nearest = nullptr;
        for (Creature* creature : found)
            if (sServerFacade.IsAlive(creature) && (!nearest || bot->GetDistance(creature) < bot->GetDistance(nearest)))
                nearest = creature;
        return nearest ? nearest->GetObjectGuid() : ObjectGuid();
    }

    // game objects in range that sparkle for the bot's quests (a chest to loot, a goober to use)
    class QuestObjectInRangeCheck
    {
    public:
        QuestObjectInRangeCheck(Player* bot, float range) : bot(bot), range(range) {}
        WorldObject const& GetFocusObject() const { return *bot; }
        bool operator()(GameObject* go)
        {
            if (go->GetGoType() != GAMEOBJECT_TYPE_CHEST && go->GetGoType() != GAMEOBJECT_TYPE_GOOBER)
                return false;
            return go->IsSpawned() && bot->IsWithinDistInMap(go, range) && go->ActivateToQuest(bot);
        }

    private:
        Player* bot;
        float range;
    };

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

QuestProvider_GlobalTask::Settings QuestProvider_GlobalTask::ParseSettings(const STAI_TaskConfig& config)
{
    Settings settings;
    settings.range = ParseFloatSetting(config, "Range", "value", settings.range);
    settings.levelsBelow = uint32(ParseFloatSetting(config, "Levels", "below", float(settings.levelsBelow)));
    settings.levelsAbove = uint32(ParseFloatSetting(config, "Levels", "above", float(settings.levelsAbove)));
    settings.nearRange = ParseFloatSetting(config, "Near", "value", settings.nearRange);
    settings.hub = ParseFloatSetting(config, "Hub", "value", settings.hub);
    settings.objectRange = ParseFloatSetting(config, "ObjectRange", "value", settings.objectRange);
    if (settings.range <= 0.0f)
        throw std::runtime_error("Range must be greater than 0");
    return settings;
}

void QuestProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseSettings(config);
}

std::unique_ptr<STAI_TaskBase> QuestProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<QuestProvider_GlobalTask>(ParseSettings(config));
}

bool QuestProvider_GlobalTask::IsWanted(Player* bot, const Quest* quest, uint32 levelsBelow, uint32 levelsAbove)
{
    if (!quest || quest->IsRepeatable() || quest->GetSuggestedPlayers() > 1)
        return false;

    switch (quest->GetType())
    {
        case QUEST_TYPE_ELITE:
        case QUEST_TYPE_PVP:
        case QUEST_TYPE_RAID:
        case QUEST_TYPE_DUNGEON:
        case QUEST_TYPE_LEGENDARY:
        case QUEST_TYPE_ESCORT:
            return false;
        default:
            break;
    }

    const uint32 level = bot->GetLevel();
    const uint32 questLevel = bot->GetQuestLevelForPlayer(quest);
    if (questLevel > level + levelsAbove || questLevel + levelsBelow < level)
        return false;
    if (!bot->CanTakeQuest(quest, false))
        return false;

    const STAI_QuestIndex& index = STAI_QuestIndex::Get();
    const std::vector<uint32>& takers = index.Takers(quest->GetQuestId());
    const bool hasTaker = std::any_of(takers.begin(), takers.end(), [&](uint32 entry) { return !index.CreatureSpawns(bot->GetMapId(), entry).empty(); });
    Needs needs;
    return hasTaker && FindNeeds(bot, quest, nullptr, needs);
}

void QuestProvider_GlobalTask::Update(QuestProvider_GlobalTaskContext& context, uint32 elapsed) const
{
    Player* bot = context.bot;
    const STAI_QuestIndex& index = STAI_QuestIndex::Get();
    const STAI_Location previousGiver = context.giverLocation;
    const STAI_Location previousTaker = context.takerLocation;
    const uint32 previousGiverEntry = context.giverEntry;
    const uint32 previousTakerEntry = context.takerEntry;
    const STAI_Location previousObjective = context.objectiveLocation;

    context.pickUp = false;
    context.available = 0;
    context.giverLocation = STAI_Location();
    context.giver = ObjectGuid();
    context.giverEntry = 0;
    context.handIn = false;
    context.completed = 0;
    context.takerLocation = STAI_Location();
    context.taker = ObjectGuid();
    context.takerEntry = 0;
    context.open = 0;
    context.quest.clear();
    context.objectiveLocation = STAI_Location();
    context.objectiveEntries.clear();
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    const uint32 mapId = bot->GetMapId();
    auto expired = [&context](const QuestProvider_GlobalTaskContext::Spot& spot) { return context.nowMs >= spot.untilMs; };
    context.ignored.erase(std::remove_if(context.ignored.begin(), context.ignored.end(), expired), context.ignored.end());
    context.visited.erase(std::remove_if(context.visited.begin(), context.visited.end(), expired), context.visited.end());

    // the bot stood at the giver / taker it was sent to without getting anything done: somebody else's turn
    auto checkStuck = [&](const STAI_Location& spot, uint32 entry, uint32& nearMs)
    {
        if (spot.IsEmpty() || spot.mapId != mapId || bot->GetDistance2d(spot.x, spot.y) > stuckRadius)
        {
            nearMs = 0;
            return;
        }
        nearMs += elapsed;
        if (nearMs < stuckMs)
            return;
        context.ignored.push_back({ entry, spot.x, spot.y, context.nowMs + ignoreSpotMs });
        nearMs = 0;
    };
    checkStuck(previousGiver, previousGiverEntry, context.nearGiverMs);
    checkStuck(previousTaker, previousTakerEntry, context.nearTakerMs);

    // reached objective spawn points are skipped for a while
    if (!previousObjective.IsEmpty() && previousObjective.mapId == mapId && bot->GetDistance2d(previousObjective.x, previousObjective.y) < reachedObjective)
        context.visited.push_back({ 0, previousObjective.x, previousObjective.y, context.nowMs + visitedForgetMs });

    // the quest log
    struct Open
    {
        const Quest* quest;
        Needs needs;
    };
    std::vector<Open> open;
    std::vector<const Quest*> completed;
    uint32 used = 0;
    uint32 hash = 0;
    const QuestStatusMap& statusMap = bot->getQuestStatusMap();
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        const uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;
        ++used;
        const Quest* quest = sObjectMgr.GetQuestTemplate(questId);
        const auto found = statusMap.find(questId);
        if (!quest || found == statusMap.end())
            continue;

        const QuestStatusData& status = found->second;
        hash = hash * 31 + questId * 7 + uint32(status.m_status);
        for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            hash = hash * 31 + status.m_creatureOrGOcount[i];
        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->ReqItemId[i])
                hash = hash * 31 + bot->GetItemCount(quest->ReqItemId[i]);

        if (status.m_status == QUEST_STATUS_COMPLETE)
            completed.push_back(quest);
        else if (status.m_status == QUEST_STATUS_INCOMPLETE)
        {
            Needs needs;
            if (FindNeeds(bot, quest, &status, needs) && !needs.Empty())
                open.push_back({ quest, needs });
        }
    }
    context.logFree = MAX_QUEST_LOG_SIZE - used;
    context.completed = uint32(completed.size());
    context.open = uint32(open.size());

    // the nearest spawn of the entries, not ignored
    auto nearest = [&](const std::vector<uint32>& entries, bool objects, Spawn& best, const std::vector<QuestProvider_GlobalTaskContext::Spot>& skip, bool skipAnyEntry)
    {
        for (uint32 entry : entries)
            for (const STAI_SpawnPoint& spot : objects ? index.ObjectSpawns(mapId, entry) : index.CreatureSpawns(mapId, entry))
            {
                const float distance = bot->GetDistance2d(spot.x, spot.y);
                if (distance >= best.distance || IsNear(skip, skipAnyEntry ? 0 : entry, spot.x, spot.y, skipAnyEntry ? visitedRadius : 1.0f))
                    continue;
                best = { entry, &spot, distance };
            }
    };

    // hand in: the nearest taker of a completed quest
    Spawn taker;
    for (const Quest* quest : completed)
        nearest(index.Takers(quest->GetQuestId()), false, taker, context.ignored, false);
    if (taker.spot)
    {
        context.takerEntry = taker.entry;
        context.takerLocation = { mapId, taker.spot->x, taker.spot->y, taker.spot->z, true };
        if (taker.distance <= nearNpc)
            context.taker = FindNpc(bot, taker.entry);

        // an open quest turned in at the same place: hand in together with it
        bool hubOpen = false;
        for (const Open& entry : open)
        {
            Spawn openTaker;
            nearest(index.Takers(entry.quest->GetQuestId()), false, openTaker, {}, false);
            if (openTaker.spot && Distance2d(openTaker.spot->x, openTaker.spot->y, taker.spot->x, taker.spot->y) <= settings.hub)
                hubOpen = true;
        }
        context.handIn = open.empty() || taker.distance <= settings.nearRange || !hubOpen;
    }

    // pick up: the nearest giver with quests for the bot
    if (context.logFree)
    {
        std::unordered_map<uint32, uint32> wanted;  // quests per giver entry, counted once
        float best = settings.range;
        const STAI_GiverSpawn* giver = nullptr;
        for (const STAI_GiverSpawn& spawn : index.GiverSpawns(mapId))
        {
            const float distance = bot->GetDistance2d(spawn.x, spawn.y);
            if (distance >= best || IsNear(context.ignored, spawn.entry, spawn.x, spawn.y, 1.0f))
                continue;

            auto known = wanted.find(spawn.entry);
            if (known == wanted.end())
            {
                uint32 count = 0;
                if (IsFriendlyNpc(bot, spawn.entry))
                {
                    const QuestRelationsMapBounds bounds = sObjectMgr.GetCreatureQuestRelationsMapBounds(spawn.entry);
                    for (auto itr = bounds.first; itr != bounds.second; ++itr)
                        if (IsWanted(bot, sObjectMgr.GetQuestTemplate(itr->second), settings.levelsBelow, settings.levelsAbove))
                            ++count;
                }
                known = wanted.emplace(spawn.entry, count).first;
            }
            if (!known->second)
                continue;

            best = distance;
            giver = &spawn;
            context.available = known->second;
        }

        if (giver)
        {
            context.giverEntry = giver->entry;
            context.giverLocation = { mapId, giver->x, giver->y, giver->z, true };
            if (best <= nearNpc)
                context.giver = FindNpc(bot, giver->entry);
            context.pickUp = open.empty() || best <= settings.nearRange;
        }
    }

    // objectives: the quest with the nearest unvisited spawn point first
    auto chooseObjective = [&]()
    {
        Spawn best;
        const Quest* chosen = nullptr;
        for (const Open& entry : open)
        {
            const float before = best.distance;
            nearest(entry.needs.creatures, false, best, context.visited, true);
            nearest(entry.needs.objects, true, best, context.visited, true);
            if (best.distance < before)
                chosen = entry.quest;
        }
        if (!chosen)
            return false;
        context.quest = chosen->GetTitle();
        context.objectiveLocation = { mapId, best.spot->x, best.spot->y, best.spot->z, true };
        return true;
    };
    // every spawn point visited: a new round (they respawn)
    if (!open.empty() && !chooseObjective())
    {
        context.visited.clear();
        chooseObjective();
    }
    for (const Open& entry : open)
        for (uint32 creature : entry.needs.creatures)
            AddUnique(context.objectiveEntries, creature);

    if (hash != context.logHash)
    {
        const bool first = context.logHash == 0;
        context.logHash = hash;
        if (!first)
            context.onQuestsChanged.Broadcast();
    }
}

void QuestProvider_GlobalTask::UpdateObjectTarget(QuestProvider_GlobalTaskContext& context, uint32 elapsed) const
{
    Player* bot = context.bot;
    const ObjectGuid previous = context.objectTarget;
    context.objectTarget = ObjectGuid();
    context.ignoredObjects.erase(std::remove_if(context.ignoredObjects.begin(), context.ignoredObjects.end(),
        [&context](const std::pair<ObjectGuid, uint32>& entry) { return context.nowMs >= entry.second; }), context.ignoredObjects.end());
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    // one the bot did not get to use in time is left alone for a while
    if (!previous.IsEmpty())
    {
        context.objectSinceMs += elapsed;
        if (context.objectSinceMs >= keepObjectMs)
        {
            context.ignoredObjects.push_back({ previous, context.nowMs + ignoreObjectMs });
            context.objectSinceMs = 0;
        }
    }

    GameObjectList found;
    QuestObjectInRangeCheck check(bot, settings.objectRange);
    MaNGOS::GameObjectListSearcher<QuestObjectInRangeCheck> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, settings.objectRange);

    GameObject* closest = nullptr;
    for (GameObject* go : found)
    {
        const ObjectGuid guid = go->GetObjectGuid();
        if (std::any_of(context.ignoredObjects.begin(), context.ignoredObjects.end(), [&guid](const std::pair<ObjectGuid, uint32>& entry) { return entry.first == guid; }))
            continue;
        if (!bot->IsWithinLOSInMap(go))
            continue;
        if (!closest || bot->GetDistance(go) < bot->GetDistance(closest))
            closest = go;
    }

    if (closest)
        context.objectTarget = closest->GetObjectGuid();
    if (context.objectTarget != previous)
        context.objectSinceMs = 0;
}

EActionRunStatus QuestProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(QuestProvider_GlobalTask::OnEnterState);
    QuestProvider_GlobalTaskContext& context = ContextAs<QuestProvider_GlobalTaskContext>(baseContext);
    context.objectTarget = ObjectGuid();
    context.nearGiverMs = 0;
    context.nearTakerMs = 0;
    context.objectSinceMs = 0;
    context.logHash = 0;
    context.sinceCheckMs = 0;
    Update(context, 0);
    UpdateObjectTarget(context, 0);
    return EActionRunStatus::Running;
}

EActionRunStatus QuestProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(QuestProvider_GlobalTask::OnTick);
    QuestProvider_GlobalTaskContext& context = ContextAs<QuestProvider_GlobalTaskContext>(baseContext);
    context.nowMs += elapsed;
    context.sinceCheckMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        const uint32 since = context.sinceCheckMs;
        context.sinceCheckMs = 0;
        Update(context, since);
        UpdateObjectTarget(context, since);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& QuestProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("pickUp", &QuestProvider_GlobalTaskContext::pickUp),
        STAI_PropertyDef::Of("available", &QuestProvider_GlobalTaskContext::available),
        STAI_PropertyDef::Of("giverLocation", &QuestProvider_GlobalTaskContext::giverLocation),
        STAI_PropertyDef::Of("giver", &QuestProvider_GlobalTaskContext::giver),
        STAI_PropertyDef::Of("handIn", &QuestProvider_GlobalTaskContext::handIn),
        STAI_PropertyDef::Of("completed", &QuestProvider_GlobalTaskContext::completed),
        STAI_PropertyDef::Of("takerLocation", &QuestProvider_GlobalTaskContext::takerLocation),
        STAI_PropertyDef::Of("taker", &QuestProvider_GlobalTaskContext::taker),
        STAI_PropertyDef::Of("open", &QuestProvider_GlobalTaskContext::open),
        STAI_PropertyDef::Of("logFree", &QuestProvider_GlobalTaskContext::logFree),
        STAI_PropertyDef::Of("quest", &QuestProvider_GlobalTaskContext::quest),
        STAI_PropertyDef::Of("objectiveLocation", &QuestProvider_GlobalTaskContext::objectiveLocation),
        STAI_PropertyDef::Of("objectiveEntries", &QuestProvider_GlobalTaskContext::objectiveEntries),
        STAI_PropertyDef::Of("objectTarget", &QuestProvider_GlobalTaskContext::objectTarget),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& QuestProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onQuestsChanged", &QuestProvider_GlobalTaskContext::onQuestsChanged) };
    return delegates;
}
