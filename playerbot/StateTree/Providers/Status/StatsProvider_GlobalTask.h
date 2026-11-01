#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of StatsProvider_GlobalTask
    struct StatsProvider_GlobalTaskContext : public STAI_TaskContext
    {
        FSTAI_Delegate onHealthChanged;
        FSTAI_Delegate onManaChanged;
        float healthPct = 100.0f;           // property "healthPct"
        float manaPct = 100.0f;             // property "manaPct"
        // per stat: the whole percentage last fired (-1: not fired yet), when
        int lastHealth = -1, lastMana = -1;
        uint32 healthFiredMs = 0, manaFiredMs = 0;
    };

    // Global task that publishes the bot's health and mana as percentages: properties healthPct, manaPct (always 100,
    // never fired, for a bot without mana: a warrior, a druid in a form), delegates onHealthChanged, onManaChanged,
    // each fired when its whole percentage changed (also on the first update after the tree started), at most once
    // per second: a change in between is fired when the second is over.
    //   <GlobalTasks>
    //     <Task class="StatsProvider_GlobalTask"/>
    //   </GlobalTasks>
    // Tickable: nothing broadcasts health / mana changes, the values are read every update. The transitions decide
    // what a change means, with their own conditions:
    //   <Transition type="OnDelegate" source="Root|StatsProvider_GlobalTask::onManaChanged" node="Root|Eat and Drink">
    //     <Conditions> <Condition binding="Root|StatsProvider_GlobalTask::manaPct" compare="lesser" value="90"/> </Conditions>
    //   </Transition>
    class StatsProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<StatsProvider_GlobalTask>(); }

        virtual const char* GetNodeClass() const override { return "StatsProvider_GlobalTask"; }
        inline static std::string task_name = "StatsProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<StatsProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static float HealthPercent(const Player* bot);
        static float ManaPercent(const Player* bot);
        // stores value; fires changed when its whole percentage changed, at most once per second
        static void UpdateStat(float value, float& percent, int& lastWhole, uint32& firedMs, FSTAI_Delegate& changed, uint32 now);
    };
}
