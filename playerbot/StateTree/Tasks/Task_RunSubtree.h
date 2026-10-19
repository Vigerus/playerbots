#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    class STAI_TreeInstance;

    // runtime context of Task_RunSubtree: the bot running the subtree
    struct Task_RunSubtreeContext : public STAI_TaskContext
    {
        Task_RunSubtreeContext();
        virtual ~Task_RunSubtreeContext();

        std::unique_ptr<STAI_TreeInstance> subtree;   // nullptr when the subtree is not loaded
    };

    // Runs another STAI tree file as the task of this node.
    //   <Task class="Task_RunSubtree">
    //     <Subtree filename="statetree/STAI_NonCombat.xml"/>
    //   </Task>
    // The subtree is the shared tree of the library (every STAI_*.xml of the statetree directory is preloaded);
    // a tree whose subtree is missing, fails to load or runs the tree itself again is not loaded.
    // The subtree is pushed on the tree stack: OnEnter starts it, OnTick ticks it (Running) until it finished
    // through a STAI_FinishTreeTask node (Succeeded, the subtree is popped); while it runs, the tree of this node
    // only ticks this node. OnExit finishes the subtree when it still runs (the bot left the state).
    // Failed when the subtree is not loaded.
    class Task_RunSubtree : public STAI_Task
    {
    public:
        explicit Task_RunSubtree(const std::string& file);

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_RunSubtree"; }

        virtual const STAI_TaskContextType& GetContextType() const override;
        virtual void InitContext(STAI_TaskContext& context) const override;

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual bool HoldsActiveTree(const STAI_TaskContext& context) const override;
        virtual std::vector<std::string> GetReferencedTrees() const override { return { file }; }

        // temporary, for compatibility: forwarded to the subtree's tasks
        virtual void UpdateStrategies(STAI_TaskContext& context, const std::string& name, BotState type) const override;
        virtual void ResetStrategies(STAI_TaskContext& context) const override;
        virtual void ReInit(STAI_TaskContext& context) const override;

        virtual IReactionChecker* GetReactionChecker(STAI_TaskContext& context) const override;

        virtual Engine* GetDebugEngine(const STAI_TaskContext& context) const override;
        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;
        virtual void AppendDebugTreeStack(const STAI_TaskContext& context, std::vector<StateNodeDebugTree>& stack) const override;

    private:
        std::string file;   // relative to the config directory
    };
}
