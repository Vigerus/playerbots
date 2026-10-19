#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Player;
class Unit;

namespace ai
{
    // runtime context of TargetProvider_GlobalTask: the bot's target
    struct TargetProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when target changed (also to none):
        //   <Transition type="OnDelegate" source="Root|TargetProvider_GlobalTask::onTargetChanged" node="..."/>
        FSTAI_Delegate onTargetChanged;
        // the unit to attack, empty when there is none:
        //   <Condition binding="Root|TargetProvider_GlobalTask::target"/>
        //   <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
        ObjectGuid target;
        bool marked = false;        // target is an rti mark (not the fallback)

        uint32 sinceCheckMs = 0;
    };

    // Global task that picks the unit the bot attacks:
    //   <Global> <Task class="TargetProvider_GlobalTask"/> </Global>
    // 1. the group's marks in the order of the bot's "rti" icons (default skull): the first marked unit that is alive,
    //    hostile and in sight;
    // 2. without a living mark, the dps assist choice: among the bot's "possible attack targets" (units fighting the
    //    bot, its group or master) the one with the least health, players excluded.
    // Crowd-controlled units (polymorph, frost, sap, ...) and the bot's "rti cc" marks are never picked, so the
    // crowd control does not break. Ticked: the current target is checked every update (it is dropped as soon as it
    // dies or gets crowd-controlled), a new choice is made every 250ms; the fallback target is kept while it stays
    // valid (no switching mid-cast whenever health values change), a living mark always takes over.
    class TargetProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<TargetProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "TargetProvider_GlobalTask"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TargetProvider_GlobalTaskContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        // alive, hostile, in sight, not crowd-controlled, not a crowd-control mark of the bot
        static bool IsValidTarget(const STAI_TaskContext& context, Unit* unit);
        static Unit* FindMarkedTarget(const STAI_TaskContext& context);
        static Unit* FindAssistTarget(const STAI_TaskContext& context);
        static void SetTarget(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked);
        void Update(TargetProvider_GlobalTaskContext& context, bool choose) const;
    };
}
