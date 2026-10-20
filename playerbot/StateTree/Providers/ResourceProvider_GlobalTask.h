#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of the resource providers: the bot's percentage and its change delegate
    struct ResourceProvider_GlobalTaskContext : public STAI_TaskContext
    {
        FSTAI_Delegate onChanged;   // onHealthChanged / onManaChanged
        float percent = 100.0f;     // healthPct / manaPct
        int lastWholePercent = -1;  // the whole percentage last fired, -1: not fired yet
        uint32 lastFiredMs = 0;
    };

    // Base of the global tasks that publish a resource of the bot as a percentage: property <resource>Pct, delegate
    // on<Resource>Changed fired when the whole percentage changed (also on the first update after the tree started), at
    // most once per second: a change in between is fired when the second is over.
    // Tickable: nothing broadcasts health / mana changes, the value is read every update. The transitions decide
    // what a change means, with their own conditions:
    //   <Transition type="OnDelegate" source="Root|ManaProvider_GlobalTask::onManaChanged" node="Root|Eat and Drink">
    //     <Conditions> <Condition binding="Root|ManaProvider_GlobalTask::manaPct" compare="lesser" value="90"/> </Conditions>
    //   </Transition>
    class ResourceProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<ResourceProvider_GlobalTaskContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }


    protected:
        virtual float ReadPercent(const Player* bot) const = 0;
    };

    // <Task class="HealthProvider_GlobalTask"/>: healthPct, onHealthChanged
    class HealthProvider_GlobalTask : public ResourceProvider_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<HealthProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "HealthProvider_GlobalTask"; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    protected:
        virtual float ReadPercent(const Player* bot) const override;
    };

    // <Task class="ManaProvider_GlobalTask"/>: manaPct, onManaChanged; always 100 (never fires) for bots without mana
    class ManaProvider_GlobalTask : public ResourceProvider_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<ManaProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "ManaProvider_GlobalTask"; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    protected:
        virtual float ReadPercent(const Player* bot) const override;
    };
}
