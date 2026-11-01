#include "Task_AcceptInvite.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotSecurity.h"

#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"
#include "Groups/Group.h"
#include "Server/WorldSession.h"

using namespace ai;

EActionRunStatus Task_AcceptInvite::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_AcceptInvite::OnEnterState);
    Player* bot = context.bot;
    Group* group = bot->GetGroupInvite();
    if (!group)
        return EActionRunStatus::Succeeded;

    StartLog(context);
    WorldPacket packet;
    Player* inviter = sObjectMgr.GetPlayer(group->GetLeaderGuid());
    if (!inviter || !context.ai->GetSecurity()->CheckLevelFor(PlayerbotSecurityLevel::PLAYERBOT_SECURITY_INVITE, false, inviter))
    {
        // tells the leader, as the client's Decline button does
        bot->GetSession()->HandleGroupDeclineOpcode(packet);
        return LogStatus(context, EActionRunStatus::Failed);
    }

    if (bot->isAFK())
        bot->ToggleAFK();

    const ObjectGuid inviterGuid = inviter->GetObjectGuid();
    bot->GetSession()->HandleGroupAcceptOpcode(packet);
    // the group may have been created by the accept: read it again
    Group* joined = bot->GetGroup();
    return LogStatus(context, joined && joined->IsMember(inviterGuid) ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}
