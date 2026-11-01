#include "Task_Corpse.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Player.h"
#include "Server/WorldSession.h"

using namespace ai;

EActionRunStatus Task_ReleaseSpirit::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_ReleaseSpirit::OnEnterState);
    Player* bot = context.bot;
    if (bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return EActionRunStatus::Succeeded;
    if (sServerFacade.IsAlive(bot) || bot->GetDeathState() == JUST_DIED)
        return EActionRunStatus::Failed;

    // as the client's button does
    StartLog(context);
    context.ai->StopMoving();
    WorldPacket packet(CMSG_REPOP_REQUEST, 1);
    packet << uint8(0);
    bot->GetSession()->HandleRepopRequestOpcode(packet);
    return LogStatus(context, bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST) ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}

EActionRunStatus Task_ReclaimCorpse::OnEnterState(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_ReclaimCorpse::OnEnterState);
    Player* bot = context.bot;
    if (sServerFacade.IsAlive(bot))
        return EActionRunStatus::Succeeded;

    // as the client's button does (the handler checks the ghost, the distance and the reclaim delay)
    StartLog(context);
    context.ai->StopMoving();
    WorldPacket packet(CMSG_RECLAIM_CORPSE, 8);
    packet << bot->GetObjectGuid();
    bot->GetSession()->HandleReclaimCorpseOpcode(packet);
    return LogStatus(context, sServerFacade.IsAlive(bot) ? EActionRunStatus::Succeeded : EActionRunStatus::Failed);
}
