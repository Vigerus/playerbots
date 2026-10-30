#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Answers the group invite the bot got (the deprecated AcceptInvitationAction), like a player clicking the popup:
    // accepts it (CMSG_GROUP_ACCEPT) when the inviting leader has the security level to invite the bot, declines it
    // (CMSG_GROUP_DECLINE) otherwise. The master then follows from the group (PlayerbotAI::UpdateAI).
    //   <Node name="Accept Invite">
    //     <Conditions> <Condition binding="Root|UIInteractionProvider_GlobalTask::groupInviteRequested"/> </Conditions>
    //     <Task class="Task_AcceptInvite"/>
    //   </Node>
    // Not tickable, done in OnEnterState: Succeeded when the bot joined the group (or there is no invite), Failed when it
    // declined or could not join (the group is full).
    class Task_AcceptInvite : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_AcceptInvite>(); }

        virtual const char* GetNodeClass() const override { return "Task_AcceptInvite"; }
        inline static std::string task_name = "AcceptInvite";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual std::string GetDebugInfo() const override { return "Accept invite"; }
    };
}
