#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/StateMachine.h"

#include <memory>

namespace ai
{
    class Engine;
    class ReactionEngine;
    class LegacyBrain;

    // Runs the legacy strategy engine of its state.
    //   <Task class="StateNode_Legacy"> <Config filename="statetree/Legacy_Combat_Mage.json"/> </Task>
    // The Legacy_*.json strategy list replaces the AiFactory defaults of the engine; without a Config the
    // AiFactory defaults stay. Never finishes on its own (Running), earlier tree nodes interrupt it.
    class StateNode_Legacy : public STAI_Task
    {
    public:
        StateNode_Legacy(const STAI_TaskContext& context, std::shared_ptr<const LegacyBrain> brain);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskContext& context, const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "StateNode_Legacy"; }

        virtual void OnEnter() override;
        virtual EActionRunStatus Update(uint32 elapsed, bool minimal) override;

        // temporary, for compatibility
        virtual void UpdateStrategies(const std::string& name, BotState type) override;
        virtual void ResetStrategies() override;
        virtual void ReInit() override;

        virtual Engine* GetDebugEngine() const override;
        virtual std::string GetDebugInfo() const override;

    private:
        Engine* GetEngine() const;
        void ApplyBrain(Engine* engine);

        PlayerbotAI* ai;
        Player* bot;
        HLSStateType stateType;
        std::shared_ptr<const LegacyBrain> brain;
    };

    // Runs the reaction engine; lets the Reaction state preempt the others.
    //   <Task class="StateNode_Reaction"> <Config filename="statetree/Legacy_Reaction_Mage.json"/> </Task>
    class StateNode_Reaction : public STAI_Task, public IReactionChecker
    {
    public:
        StateNode_Reaction(const STAI_TaskContext& context, std::shared_ptr<const LegacyBrain> brain);

        static std::unique_ptr<STAI_Task> Create(const STAI_TaskContext& context, const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "StateNode_Reaction"; }

        virtual void OnExit() override;
        virtual EActionRunStatus Update(uint32 elapsed, bool minimal) override;

        // temporary, for compatibility
        virtual void UpdateStrategies(const std::string& name, BotState type) override;
        virtual void ResetStrategies() override;
        virtual void ReInit() override;

        virtual IReactionChecker* GetReactionChecker() override { return this; }
        virtual bool ShouldEnterReaction() override;
        virtual bool IsReacting() override;

        virtual Engine* GetDebugEngine() const override;
        virtual std::string GetDebugInfo() const override;

    private:
        bool UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned);
        void ApplyBrain(Engine* engine);

        PlayerbotAI* ai;
        Player* bot;
        std::shared_ptr<const LegacyBrain> brain;
        std::unique_ptr<ReactionEngine> reactionEngine;
    };
}
