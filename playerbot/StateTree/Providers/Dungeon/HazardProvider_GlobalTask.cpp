#include "HazardProvider_GlobalTask.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/DynamicObject.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Maps/Map.h"
#include "Spells/SpellMgr.h"

#include <cmath>
#include <stdexcept>

using namespace ai;

namespace
{
    // how often the area effects are checked
    constexpr uint32 checkIntervalMs = 250;
    // onHazard fires again this often while the bot is still in a hazard
    constexpr uint32 refireIntervalMs = 1000;
    // area effects this far from the bot are looked at (the largest ones are around 30 yards wide)
    constexpr float searchRange = 40.0f;
    // the spots tried around the bot
    constexpr int directions = 16;
    constexpr float stepDistances[] = { 4.0f, 7.0f, 10.0f, 14.0f, 20.0f };
    // a spot this much higher or lower than the bot is not walked to
    constexpr float maxHeightDiff = 4.0f;

    // the area effects hostile to the bot with a harmful effect
    struct HostileHazardCheck
    {
        Player* bot;

        bool operator()(DynamicObject* dynObject) const
        {
            if (!dynObject->IsInWorld() || dynObject->GetRadius() <= 0.0f || !bot->IsWithinDist(dynObject, searchRange))
                return false;

            const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(dynObject->GetSpellId());
            if (!spellInfo || IsPositiveEffect(spellInfo, dynObject->GetEffIndex()))
                return false;

            // the bot's own and its group's effects (Blizzard, Consecration) are not hazards
            Unit* caster = dynObject->GetCaster();
            return caster && sServerFacade.IsHostileTo(bot, caster);
        }
    };
}

float HazardProvider_GlobalTask::ParseMargin(const STAI_TaskConfig& config)
{
    const std::string value = STAI_TaskFactory::GetSetting(config, "Margin").value_or("2");
    char* end = nullptr;
    const float margin = std::strtof(value.c_str(), &end);
    if (value.empty() || *end || margin < 0.0f)
        throw std::runtime_error("<Margin value=\"" + value + "\">: needs yards (0 or more)");
    return margin;
}

void HazardProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseMargin(config);
}

std::unique_ptr<STAI_TaskBase> HazardProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<HazardProvider_GlobalTask>(ParseMargin(config));
}

std::vector<DynamicObject*> HazardProvider_GlobalTask::FindHazards(Player* bot)
{
    DynamicObjectList found;
    HostileHazardCheck check{ bot };
    MaNGOS::DynamicObjectListSearcher<HostileHazardCheck> searcher(found, check);
    Cell::VisitGridObjects(bot, searcher, searchRange);
    return std::vector<DynamicObject*>(found.begin(), found.end());
}

bool HazardProvider_GlobalTask::IsSafe(float x, float y, const std::vector<DynamicObject*>& hazards) const
{
    for (DynamicObject* hazard : hazards)
    {
        const float dx = hazard->GetPositionX() - x;
        const float dy = hazard->GetPositionY() - y;
        const float reach = hazard->GetRadius() + margin;
        if (dx * dx + dy * dy < reach * reach)
            return false;
    }
    return true;
}

STAI_Position HazardProvider_GlobalTask::FindSafePosition(Player* bot, const std::vector<DynamicObject*>& hazards) const
{
    // the nearest spot first: every direction at a distance before the next distance
    Map* map = bot->GetMap();
    for (float distance : stepDistances)
    {
        for (int i = 0; i < directions; ++i)
        {
            const float angle = bot->GetOrientation() + float(i) * 2.0f * float(M_PI) / float(directions);
            const float x = bot->GetPositionX() + distance * std::cos(angle);
            const float y = bot->GetPositionY() + distance * std::sin(angle);
            if (!IsSafe(x, y, hazards))
                continue;

            const float z = map->GetHeight(x, y, bot->GetPositionZ() + maxHeightDiff);
            if (z <= INVALID_HEIGHT || std::fabs(z - bot->GetPositionZ()) > maxHeightDiff)
                continue;
            if (!bot->IsWithinLOS(x, y, z + 1.0f))
                continue;

            return { x, y, z, true };
        }
    }
    return {};
}

void HazardProvider_GlobalTask::Update(HazardProvider_GlobalTaskContext& context) const
{
    Player* bot = context.bot;
    const bool wasInHazard = context.inHazard;
    context.inHazard = false;

    std::vector<DynamicObject*> hazards;
    if (bot->IsInWorld() && !bot->IsBeingTeleported() && sServerFacade.IsAlive(bot))
        hazards = FindHazards(bot);

    for (DynamicObject* hazard : hazards)
    {
        if (hazard->IsAffecting(bot) || bot->GetDistance2d(hazard->GetPositionX(), hazard->GetPositionY(), DIST_CALC_NONE) < hazard->GetRadius())
        {
            context.inHazard = true;
            break;
        }
    }

    if (!context.inHazard)
    {
        context.safePosition = {};
        return;
    }

    // keep the spot the bot is going to while it stays safe, so its move is not ordered again
    if (!context.safePosition.valid || !IsSafe(context.safePosition.x, context.safePosition.y, hazards))
        context.safePosition = FindSafePosition(bot, hazards);

    if (!wasInHazard || context.sinceFireMs >= refireIntervalMs)
    {
        context.sinceFireMs = 0;
        context.onHazard.Broadcast();
    }
}

EActionRunStatus HazardProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HazardProvider_GlobalTask::OnEnterState);
    HazardProvider_GlobalTaskContext& context = ContextAs<HazardProvider_GlobalTaskContext>(baseContext);
    context.inHazard = false;
    context.safePosition = {};
    context.sinceCheckMs = 0;
    context.sinceFireMs = 0;
    Update(context);
    return EActionRunStatus::Running;
}

EActionRunStatus HazardProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(HazardProvider_GlobalTask::OnTick);
    HazardProvider_GlobalTaskContext& context = ContextAs<HazardProvider_GlobalTaskContext>(baseContext);
    context.sinceCheckMs += elapsed;
    context.sinceFireMs += elapsed;
    if (context.sinceCheckMs >= checkIntervalMs)
    {
        context.sinceCheckMs = 0;
        Update(context);
    }
    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& HazardProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("inHazard", &HazardProvider_GlobalTaskContext::inHazard),
        STAI_PropertyDef::Of("safePosition", &HazardProvider_GlobalTaskContext::safePosition),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& HazardProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onHazard", &HazardProvider_GlobalTaskContext::onHazard) };
    return delegates;
}
