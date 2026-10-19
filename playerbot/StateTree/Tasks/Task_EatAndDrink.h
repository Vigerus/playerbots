#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <string>

namespace ai
{
    // runtime context of Task_EatAndDrink
    struct Task_EatAndDrinkContext : public STAI_TaskContext
    {
        uint32 lastActiveMs = 0;    // last time the bot ate or drank (or the task was entered)
    };

    // Eats and drinks until health and mana reach the given percentages:
    //   <Task class="Task_EatAndDrink">
    //     <Health until="100"/>     percent, default 100
    //     <Mana until="100"/>       percent, default 100; ignored for bots without mana
    //   </Task>
    // Eating is for everyone, drinking for mana users; both run in parallel. Uses the legacy food / drink actions
    // (food and water from the bags, or the bot's eat / drink cheat), not their thresholds: a resource below its
    // target is eaten / drunk for, again when the food or drink ran out before it got there. Succeeded once both
    // targets are reached; Failed when it has nothing to eat or drink, or neither eats nor drinks for 3s; OnExit stands
    // the bot up, which ends the food and drink.
    class Task_EatAndDrink : public STAI_Task
    {
    public:
        Task_EatAndDrink(float healthUntil, float manaUntil) : healthUntil(healthUntil), manaUntil(manaUntil) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_EatAndDrink"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_EatAndDrinkContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        bool NeedsHealth(const Player* bot) const;
        bool NeedsMana(const Player* bot) const;
        // starts eating / drinking for what is below its target and not being restored yet; true when the bot eats or drinks
        bool Consume(Task_EatAndDrinkContext& context) const;

        float healthUntil;
        float manaUntil;
    };
}
