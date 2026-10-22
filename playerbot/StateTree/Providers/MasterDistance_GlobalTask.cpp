#include "MasterDistance_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <regex>
#include <stdexcept>

using namespace ai;

namespace
{
    const std::regex numberPattern(R"(^\d+(?:\.\d+)?$)");
    const std::regex rangePattern(R"(^[a-z][a-z ]*$)");

    // yards or a range name for ai->GetRange(); default: the bot's follow distance
    std::string ParseRange(const STAI_TaskConfig& config)
    {
        const std::string value = STAI_TaskFactory::GetSetting(config, "Range").value_or("follow");
        if (std::regex_match(value, numberPattern))
        {
            if (std::stof(value) <= 0.0f)
                throw std::runtime_error("Range must be greater than 0");
            return value;
        }

        if (!std::regex_match(value, rangePattern))
            throw std::runtime_error("Range: expected yards or a range name such as \"follow\", got '" + value + "'");
        return value;
    }
}

void MasterDistance_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_Task> MasterDistance_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<MasterDistance_GlobalTask>(ParseRange(config));
}

EActionRunStatus MasterDistance_GlobalTask::OnEnter(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MasterDistance_GlobalTask::OnEnter);
    ContextAs<MasterDistance_GlobalTaskContext>(baseContext).masterFar = false;
    ContextAs<MasterDistance_GlobalTaskContext>(baseContext).location = STAI_Location();
    return EActionRunStatus::Running;
}

EActionRunStatus MasterDistance_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(MasterDistance_GlobalTask::OnTick);
    MasterDistance_GlobalTaskContext& context = ContextAs<MasterDistance_GlobalTaskContext>(baseContext);
    Player* bot = context.bot;
    Player* master = context.ai->GetMaster();

    context.masterFar = false;
    context.location = STAI_Location();
    if (master && master != bot && master->IsInWorld() && !bot->IsBeingTeleported() && !master->IsBeingTeleported())
    {
        context.location = { master->GetMapId(), master->GetPositionX(), master->GetPositionY(), master->GetPositionZ(), true };

        // on another map or instance (the master entered or left a dungeon) the master is far as long as it lives:
        // the path to it goes through the portals; a released ghost is not followed to its graveyard
        if (master->GetMapId() != bot->GetMapId() || master->GetInstanceId() != bot->GetInstanceId())
            context.masterFar = master->IsAlive();
        else
        {
            const float yards = std::regex_match(range, numberPattern) ? std::stof(range) : context.ai->GetRange(range);
            context.masterFar = sServerFacade.GetDistance2d(bot, master) > yards;
        }
    }

    if (context.masterFar)
        context.onMasterFar.Broadcast();

    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& MasterDistance_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("masterFar", &MasterDistance_GlobalTaskContext::masterFar),
        STAI_PropertyDef::Of("location", &MasterDistance_GlobalTaskContext::location),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& MasterDistance_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onMasterFar", &MasterDistance_GlobalTaskContext::onMasterFar) };
    return delegates;
}

