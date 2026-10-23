#pragma once

#include "playerbot/StateTree/Providers/STAI_TeamRoles.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of TeamProvider_GlobalTask: the roles of the bot's group
    struct TeamProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when a member's spec or role changed, a member joined or left (and on the first update):
        //   <Transition type="OnDelegate" source="Root|TeamProvider_GlobalTask::onRolesChanged" node="Root"/>
        FSTAI_Delegate onRolesChanged;
        // the members (the bot included) on the bot's map by role, in group order:
        //   <Condition binding="Root|TeamProvider_GlobalTask::tanks"/>
        std::vector<ObjectGuid> tanks;
        std::vector<ObjectGuid> healers;
        std::vector<ObjectGuid> damage;
        // the first living tank other than the bot, empty without one:
        //   <Binding local="target" external="Root|TeamProvider_GlobalTask::mainTank"/>
        ObjectGuid mainTank;
        // the bot's own spec (SpecProvider_GlobalTask names) and roles
        std::string specName;
        bool isTank = false;
        bool isHealer = false;

        STAI_TeamRoles roles;
    };

    // Global task that publishes the spec and roles of the bot's group (STAI_TeamRoles: PlayerSpec, read again on the
    // TeamSubsystem's group and talent events and every 2s for stances):
    //   <Global>
    //     <Task class="TeamProvider_GlobalTask"/>
    //   </Global>
    // Properties tanks, healers, damage, mainTank, specName, isTank, isHealer; delegate onRolesChanged. Ticked.
    class TeamProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<TeamProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "TeamProvider_GlobalTask"; }
        inline static std::string task_name = "TeamProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TeamProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        // the first living tank of roles other than the bot on its map, empty without one
        static ObjectGuid FindMainTank(Player* bot, const STAI_TeamRoles& roles);

    private:
        static void Publish(TeamProvider_GlobalTaskContext& context);
    };
}
