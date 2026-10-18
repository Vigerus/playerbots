#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/StateMachine.h"

#include <memory>

namespace ai
{
    class Engine;
    class ReactionEngine;
    class LegacyBrain;

    // runtime context of the legacy tasks: the logged entry of the action executed last
    struct StateNode_LegacyContext : public STAI_TaskContext
    {
        uint64 lastActionEntry = 0;
    };

    // Runs the legacy strategy engine of its state.
    //   <Task class="StateNode_Legacy"> <Config filename="statetree/Legacy_Combat_Mage.json"/> </Task>
    // The Legacy_*.json strategy list replaces the AiFactory defaults of the engine; without a Config the
    // AiFactory defaults stay. Never finishes on its own (Running), earlier tree nodes interrupt it.
    class StateNode_Legacy : public STAI_Task
    {
    public:
        explicit StateNode_Legacy(std::shared_ptr<const LegacyBrain> brain);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "StateNode_Legacy"; }
        virtual bool LogsNodeActivity() const override { return false; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<StateNode_LegacyContext>(); }
        virtual void InitContext(STAI_TaskContext& context) const override;

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;

        // temporary, for compatibility
        virtual void UpdateStrategies(STAI_TaskContext& context, const std::string& name, BotState type) const override;
        virtual void ResetStrategies(STAI_TaskContext& context) const override;
        virtual void ReInit(STAI_TaskContext& context) const override;

        virtual Engine* GetDebugEngine(const STAI_TaskContext& context) const override;
        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        static Engine* GetEngine(const STAI_TaskContext& context);
        void ApplyBrain(const STAI_TaskContext& context, Engine* engine) const;

        std::shared_ptr<const LegacyBrain> brain;
    };

    // runtime context of StateNode_Reaction: the bot's reaction engine
    struct StateNode_ReactionContext : public StateNode_LegacyContext, public IReactionChecker
    {
        StateNode_ReactionContext();
        virtual ~StateNode_ReactionContext();

        virtual bool ShouldEnterReaction() override;
        virtual bool IsReacting() override;

        std::unique_ptr<ReactionEngine> reactionEngine;
    };

    // Runs the reaction engine; lets the Reaction state preempt the others.
    //   <Task class="StateNode_Reaction"> <Config filename="statetree/Legacy_Reaction_Mage.json"/> </Task>
    class StateNode_Reaction : public STAI_Task
    {
    public:
        explicit StateNode_Reaction(std::shared_ptr<const LegacyBrain> brain);

        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "StateNode_Reaction"; }
        virtual bool LogsNodeActivity() const override { return false; }

        virtual const STAI_TaskContextType& GetContextType() const override;
        virtual void InitContext(STAI_TaskContext& context) const override;

        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;

        // temporary, for compatibility
        virtual void UpdateStrategies(STAI_TaskContext& context, const std::string& name, BotState type) const override;
        virtual void ResetStrategies(STAI_TaskContext& context) const override;
        virtual void ReInit(STAI_TaskContext& context) const override;

        virtual IReactionChecker* GetReactionChecker(STAI_TaskContext& context) const override;

        virtual Engine* GetDebugEngine(const STAI_TaskContext& context) const override;
        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

    private:
        static bool UpdateAIReaction(StateNode_ReactionContext& context, uint32 elapsed, bool minimal, bool isStunned);
        void ApplyBrain(const STAI_TaskContext& context, Engine* engine) const;
        void ApplyBrainOrDefaults(StateNode_ReactionContext& context) const;

        std::shared_ptr<const LegacyBrain> brain;
    };
}
