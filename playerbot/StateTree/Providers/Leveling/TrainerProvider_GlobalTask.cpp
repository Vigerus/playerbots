#include "TrainerProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Providers/Leveling/STAI_SpawnIndex.h"

#include "Entities/Creature.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"

#include <algorithm>
#include <stdexcept>

using namespace ai;

namespace
{
    constexpr uint32 checkIntervalMs = 5000;
    constexpr float nearTrainer = 50.0f;    // yards: the trainer creature is looked up (for its guid)
}

float TrainerProvider_GlobalTask::ParseRange(const STAI_TaskConfig& config)
{
    const std::string text = STAI_TaskFactory::GetSetting(config, "Range").value_or("2000");
    char* end = nullptr;
    const float value = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end || value <= 0.0f)
        throw std::runtime_error("<Range value=\"" + text + "\">: expected yards greater than 0");
    return value;
}

void TrainerProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_TaskBase> TrainerProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<TrainerProvider_GlobalTask>(ParseRange(config));
}

bool TrainerProvider_GlobalTask::IsClassTrainerFor(const Player* bot, const CreatureInfo* info)
{
    if (!info || !(info->NpcFlags & UNIT_NPC_FLAG_TRAINER) || info->TrainerType != TRAINER_TYPE_CLASS || info->TrainerClass != bot->getClass())
        return false;

    const FactionTemplateEntry* faction = sFactionTemplateStore.LookupEntry(info->Faction);
    const FactionTemplateEntry* botFaction = bot->GetFactionTemplateEntry();
    return faction && botFaction && !faction->IsHostileTo(*botFaction);
}

std::vector<TrainerProvider_GlobalTask::Learnable> TrainerProvider_GlobalTask::FindLearnable(Player* bot, const CreatureInfo* trainer)
{
    std::vector<Learnable> spells;
    if (!trainer)
        return spells;

    // the same checks as the trainer window (WorldSession::HandleTrainerBuySpellOpcode)
    auto add = [&](const TrainerSpellData* data)
    {
        if (!data)
            return;
        for (const auto& [id, trainerSpell] : data->spellList)
        {
            uint32 reqLevel = 0;
            if (!bot->IsSpellFitByClassAndRace(trainerSpell.learnedSpell, &reqLevel))
                continue;
            reqLevel = trainerSpell.isProvidedReqLevel ? trainerSpell.reqLevel : std::max(reqLevel, trainerSpell.reqLevel);
            if (bot->GetTrainerSpellState(&trainerSpell, reqLevel) == TRAINER_SPELL_GREEN)
                spells.push_back({ trainerSpell.spell, trainerSpell.spellCost });
        }
    };
    add(sObjectMgr.GetNpcTrainerSpells(trainer->Entry));
    if (trainer->TrainerTemplateId)
        add(sObjectMgr.GetNpcTrainerTemplateSpells(trainer->TrainerTemplateId));

    std::sort(spells.begin(), spells.end(), [](const Learnable& a, const Learnable& b) { return a.cost < b.cost; });
    return spells;
}

void TrainerProvider_GlobalTask::Update(TrainerProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const uint32 hadSpells = context.newSpells;
    context.newSpells = 0;
    context.cost = 0;
    context.trainer = ObjectGuid();
    if (!bot->IsInWorld() || !sServerFacade.IsAlive(bot))
        return;

    // the nearest trainer spawn, looked for again after a map change or when there was none
    if (!context.trainerEntry || context.mapId != bot->GetMapId())
    {
        context.trainerEntry = 0;
        context.mapId = bot->GetMapId();
        context.trainerLocation = STAI_Location();
        float best = range;
        for (const STAI_Spawn& spawn : STAI_GetSpawns(bot->GetMapId()))
        {
            const float distance = bot->GetDistance2d(spawn.x, spawn.y);
            if (distance >= best || !IsClassTrainerFor(bot, ObjectMgr::GetCreatureTemplate(spawn.entry)))
                continue;
            best = distance;
            context.trainerEntry = spawn.entry;
            context.trainerLocation = { bot->GetMapId(), spawn.x, spawn.y, spawn.z, true };
        }
    }

    context.trainerDistance = context.trainerLocation.IsEmpty() ? 0 : uint32(bot->GetDistance2d(context.trainerLocation.x, context.trainerLocation.y));

    // what the bot's money pays for, cheapest first
    const uint32 money = bot->GetMoney();
    for (const Learnable& spell : FindLearnable(bot, ObjectMgr::GetCreatureTemplate(context.trainerEntry)))
    {
        if (context.cost + spell.cost > money)
            break;
        context.cost += spell.cost;
        ++context.newSpells;
    }

    if (context.trainerEntry && context.trainerDistance <= nearTrainer)
    {
        CreatureList found;
        MaNGOS::AllCreaturesOfEntryInRangeCheck check(bot, context.trainerEntry, nearTrainer);
        MaNGOS::CreatureListSearcher<MaNGOS::AllCreaturesOfEntryInRangeCheck> searcher(found, check);
        Cell::VisitGridObjects(bot, searcher, nearTrainer);
        for (Creature* creature : found)
            if (sServerFacade.IsAlive(creature))
                context.trainer = creature->GetObjectGuid();
    }

    if (context.newSpells && !hadSpells)
        context.onNewSpells.Broadcast();
}

EActionRunStatus TrainerProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TrainerProvider_GlobalTask::OnEnterState);
    TrainerProvider_GlobalTaskContext& context = ContextAs<TrainerProvider_GlobalTaskContext>(baseContext);
    context.newSpells = 0;
    context.cost = 0;
    context.trainer = ObjectGuid();
    context.trainerEntry = 0;
    context.sinceCheckMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus TrainerProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(TrainerProvider_GlobalTask::OnTick);
    TrainerProvider_GlobalTaskContext& context = ContextAs<TrainerProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    // near the trainer every second, so the range goal gets its guid in time
    if (context.sinceCheckMs >= checkIntervalMs || (context.trainerDistance && context.trainerDistance <= nearTrainer && context.sinceCheckMs >= 1000))
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& TrainerProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("newSpells", &TrainerProvider_GlobalTaskContext::newSpells),
        STAI_PropertyDef::Of("cost", &TrainerProvider_GlobalTaskContext::cost),
        STAI_PropertyDef::Of("trainerLocation", &TrainerProvider_GlobalTaskContext::trainerLocation),
        STAI_PropertyDef::Of("trainerDistance", &TrainerProvider_GlobalTaskContext::trainerDistance),
        STAI_PropertyDef::Of("trainer", &TrainerProvider_GlobalTaskContext::trainer),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& TrainerProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onNewSpells", &TrainerProvider_GlobalTaskContext::onNewSpells) };
    return delegates;
}
