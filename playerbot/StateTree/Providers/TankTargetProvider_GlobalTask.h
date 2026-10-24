#pragma once

#include "playerbot/StateTree/Providers/STAI_TargetAuraWatch.h"
#include "playerbot/StateTree/Providers/STAI_TeamRoles.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

class Player;
class Unit;

namespace ai
{
    // runtime context of TankTargetProvider_GlobalTask: what the tanking bot holds
    struct TankTargetProvider_GlobalTaskContext : public STAI_TaskContext
    {
        ~TankTargetProvider_GlobalTaskContext() override { roles.Unsubscribe(); auraWatch.Unsubscribe(); }

        // fired when target changed (also to none):
        //   <Transition type="OnDelegate" source="Root|TankTargetProvider_GlobalTask::onTargetChanged" node="Root"/>
        FSTAI_Delegate onTargetChanged;
        // fired when a new unit attacks a member that is no tank:
        //   <Transition type="OnDelegate" source="Root|TankTargetProvider_GlobalTask::onLoose" node="Root|Taunt"/>
        FSTAI_Delegate onLoose;
        // fired when an entry of auraStacks changed (also by a target change):
        //   <Transition type="OnDelegate" source="Root|TankTargetProvider_GlobalTask::onAuraChanged" node="..."/>
        FSTAI_Delegate onAuraChanged;

        // properties:
        //   target: the unit to hold (see the class comment), empty when there is none
        //   taunt: target when it attacks a member that is no tank (Taunt; never off another tank)
        //   loose: the units attacking members that are no tank (healers first)
        //   mainTank: the bot is the group's main tank (the first living tank of the group), else it is an off tank
        //   auraStacks: per <Aura spellid="..."/>, its stack on target (any rank, any caster), 0 without it, by key:
        //     <Condition binding="Root|TankTargetProvider_GlobalTask::auraStacks" key="7386" compare="lesser" value="5"/>
        ObjectGuid target;
        ObjectGuid taunt;
        std::vector<ObjectGuid> loose;
        bool mainTank = false;
        std::map<uint32, uint32> auraStacks;

        STAI_TeamRoles roles;
        STAI_TargetAuraWatch auraWatch;
        uint32 sinceCheckMs = 0;
    };

    // Global task of tank trees: the unit the bot holds, among the units fighting the group (players and the bot's
    // crowd-control marks excluded, crowd-controlled units too):
    //   <Global> <Task class="TankTargetProvider_GlobalTask"/> </Global>
    // Main tank: a unit attacking a member that is no tank (the healers' first, the closest), else the group's rti mark,
    // else the current target while it is valid, else the closest unit that does not attack the bot yet, else the
    // closest one. Off tank (another living tank comes first in the group): a loose unit that does not attack the
    // main tank, else the current target while it does not attack the main tank, else a unit attacking the bot, else
    // the main tank's target (assist). Ticked: chooses every 250ms.
    // Optional: <Aura spellid="7386"/> per aura watched on target (auraStacks, onAuraChanged; STAI_TargetAuraWatch).
    class TankTargetProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        explicit TankTargetProvider_GlobalTask(std::vector<STAI_WatchedAura> auras) : auras(std::move(auras)) {}

        static void Validate(const STAI_TaskConfig& config) { STAI_TargetAuraWatch::Parse(config); }
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<TankTargetProvider_GlobalTask>(STAI_TargetAuraWatch::Parse(config)); }

        virtual const char* GetTaskClass() const override { return "TankTargetProvider_GlobalTask"; }
        inline static std::string task_name = "TankTargetProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TankTargetProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static void Choose(TankTargetProvider_GlobalTaskContext& context);
        void UpdateAuras(TankTargetProvider_GlobalTaskContext& context) const;

        std::vector<STAI_WatchedAura> auras;
    };
}
