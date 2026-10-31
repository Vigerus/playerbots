#include "LeaderProvider_GlobalTask.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Groups/Group.h"

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

void LeaderProvider_GlobalTask::Validate(const STAI_TaskConfig& config)
{
    ParseRange(config);
}

std::unique_ptr<STAI_TaskBase> LeaderProvider_GlobalTask::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<LeaderProvider_GlobalTask>(ParseRange(config));
}

Player* LeaderProvider_GlobalTask::FindLeader(PlayerbotAI* ai, Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;

    // TODO(leader): a bot leader giving orders to the master (positions, decurse, pulls) changes this priority
    Player* master = ai->GetMaster();
    if (master && master != bot && group->IsMember(master->GetObjectGuid()))
        return master;

    if (group->GetLeaderGuid() == bot->GetObjectGuid())
        return nullptr;

    return sObjectMgr.GetPlayer(group->GetLeaderGuid());
}

EActionRunStatus LeaderProvider_GlobalTask::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LeaderProvider_GlobalTask::OnEnterState);
    LeaderProvider_GlobalTaskContext& context = ContextAs<LeaderProvider_GlobalTaskContext>(baseContext);
    context.leader = ObjectGuid();
    context.hasLeader = false;
    context.leaderIsMaster = false;
    context.leaderFar = false;
    context.location = STAI_Location();
    return EActionRunStatus::Running;
}

EActionRunStatus LeaderProvider_GlobalTask::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(LeaderProvider_GlobalTask::OnTick);
    LeaderProvider_GlobalTaskContext& context = ContextAs<LeaderProvider_GlobalTaskContext>(baseContext);
    Player* bot = context.bot;
    Player* leader = FindLeader(context.ai, bot);

    const ObjectGuid leaderGuid = leader ? leader->GetObjectGuid() : ObjectGuid();
    const bool changed = leaderGuid != context.leader;
    context.leader = leaderGuid;
    context.hasLeader = leader != nullptr;
    context.leaderIsMaster = leader && leader == context.ai->GetMaster();

    context.leaderFar = false;
    context.location = STAI_Location();
    if (leader && leader->IsInWorld() && !bot->IsBeingTeleported() && !leader->IsBeingTeleported())
    {
        context.location = { leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(), leader->GetPositionZ(), true };

        // on another map or instance (the leader entered or left a dungeon) the leader is far as long as it lives:
        // the path to it goes through the portals; a released ghost is not followed to its graveyard
        if (leader->GetMapId() != bot->GetMapId() || leader->GetInstanceId() != bot->GetInstanceId())
            context.leaderFar = leader->IsAlive();
        else
        {
            const float yards = std::regex_match(range, numberPattern) ? std::stof(range) : context.ai->GetRange(range);
            context.leaderFar = sServerFacade.GetDistance2d(bot, leader) > yards;
        }
    }

    if (changed)
        context.onLeaderChanged.Broadcast();

    if (context.leaderFar)
        context.onLeaderFar.Broadcast();

    return EActionRunStatus::Running;
}

const std::vector<STAI_PropertyDef>& LeaderProvider_GlobalTask::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties =
    {
        STAI_PropertyDef::Of("leader", &LeaderProvider_GlobalTaskContext::leader),
        STAI_PropertyDef::Of("hasLeader", &LeaderProvider_GlobalTaskContext::hasLeader),
        STAI_PropertyDef::Of("leaderIsMaster", &LeaderProvider_GlobalTaskContext::leaderIsMaster),
        STAI_PropertyDef::Of("leaderFar", &LeaderProvider_GlobalTaskContext::leaderFar),
        STAI_PropertyDef::Of("location", &LeaderProvider_GlobalTaskContext::location),
    };
    return properties;
}

const std::vector<STAI_DelegateDef>& LeaderProvider_GlobalTask::GetDelegates() const
{
    static const std::vector<STAI_DelegateDef> delegates =
    {
        STAI_DelegateDef::Of("onLeaderFar", &LeaderProvider_GlobalTaskContext::onLeaderFar),
        STAI_DelegateDef::Of("onLeaderChanged", &LeaderProvider_GlobalTaskContext::onLeaderChanged),
    };
    return delegates;
}
