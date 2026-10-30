#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // Passes with a chance on every test (RedStateTree's Random Chance):
    //   <Condition class="Condition_RandomChance" chance="25"/>      percent, 0..100
    class Condition_RandomChance : public STAI_ConditionBase
    {
    public:
        explicit Condition_RandomChance(float chance) : chance(chance) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConditionBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Condition_RandomChance"; }
        virtual std::string GetDebugInfo() const override;

    protected:
        virtual bool OnTestCondition(STAI_NodeContext& context) const override;

    private:
        float chance;
    };

    // Where the active leaf state is relative to the state the condition belongs to (RedStateTree's States Order /
    // Don't Reenter); states count in file order, depth first:
    //   <Condition class="Condition_ActiveStateLocation" location="above,below"/>
    // above: the active leaf comes before the state, this: the state is active, below: the active leaf comes after
    // the state and its children. Default above,below: passes unless the state or one below it runs (don't reenter).
    // Passes when no state is active or the condition belongs to no state (a global task).
    class Condition_ActiveStateLocation : public STAI_ConditionBase
    {
    public:
        enum Location : uint8 { Above = 1, This = 2, Below = 4 };

        explicit Condition_ActiveStateLocation(uint8 mask) : mask(mask) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConditionBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Condition_ActiveStateLocation"; }
        virtual std::string GetDebugInfo() const override;

    protected:
        virtual bool OnTestCondition(STAI_NodeContext& context) const override;

    private:
        uint8 mask;
    };

    // Passes once the state the condition belongs to has been active for a time (an OnTick transition leaving it):
    //   <Condition class="Condition_TimeInState" ms="5000"/>
    class Condition_TimeInState : public STAI_ConditionBase
    {
    public:
        explicit Condition_TimeInState(uint32 ms) : ms(ms) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConditionBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Condition_TimeInState"; }
        virtual std::string GetDebugInfo() const override { return "in state " + std::to_string(ms) + " ms"; }

    protected:
        virtual bool OnTestCondition(STAI_NodeContext& context) const override;

    private:
        uint32 ms;
    };
}
