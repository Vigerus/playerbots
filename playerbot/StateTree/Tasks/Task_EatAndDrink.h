#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <string>

namespace ai
{
    // runtime context of Task_EatAndDrink: one meal, from the first bite to the end of the food / drink
    struct Task_EatAndDrinkContext : public STAI_TaskContext
    {
        uint32 startMs = 0;         // the task entered or the last food / drink started
        bool eatStarted = false;    // a food started, its aura is not seen yet
        bool drinkStarted = false;
        bool consumed = false;      // the bot ate or drank during this meal
    };

    // One meal: eats and drinks in parallel until health and mana reach the given percentages or the food and drink
    // run out:
    //   <Task class="Task_EatAndDrink">
    //     <Health until="100"/>     percent, default 100
    //     <Mana until="100"/>       percent, default 100; ignored for bots without mana
    //   </Task>
    // Eating is for everyone, drinking for mana users. Uses a food / drink item from the bags, or with the item cheat
    // the use spell of the conjured food / drink of the bot's level; both sit the bot down and apply the food / drink
    // aura.
    // A food / drink that ends before its target is followed by the next one. Succeeded when both targets are reached
    // or nothing is left to eat or drink after the bot ate or drank; Failed when nothing could be eaten or drunk
    // within 3s (nothing in the bags, the bot kept moving). OnExit stands the bot up, which ends the food and drink.
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

        virtual std::string GetDebugInfo() const override;

    private:
        bool NeedsHealth(const Player* bot) const;
        bool NeedsMana(const Player* bot) const;
        // runs the food / drink action for what is below its target, each at most once per meal
        void Start(Task_EatAndDrinkContext& context) const;

        float healthUntil;
        float manaUntil;
    };
}
