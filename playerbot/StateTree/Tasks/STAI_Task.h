#pragma once

#include "playerbot/StateTree/STAI_NodeBase.h"

#include <optional>

namespace ai
{
    class STAI_TreeAsset;

    // Task of a STAI state tree (RedStateTree's FRedSTTaskBase), a <Task class="..."> of a state or of <GlobalTasks>.
    // Created once per tree file when it is loaded and shared by every bot running that tree: tasks hold only their
    // configuration, everything per bot lives in the context the tree constructs for the task (GetContextType).
    // Every task of the active states runs at the same time. Lifecycle (the tree calls the wrappers, tasks override
    // the On* hooks): OnEnterState when its state is entered, which already finishes the task when it returns
    // Succeeded / Failed; Running: OnTick every bot update (ShouldCallTick tasks only) until it returns Succeeded or
    // Failed; a task that does not tick keeps running until its state is left. A finished task completes its state
    // according to its completion (ESTAI_TaskCompletion); OnStateCompleted when its state completed; OnExitState when
    // the state is left (completed, a transition, the tree finishing). Per bot setup is done by the task in
    // OnEnterState. The task's <Binding>s are copied into its context before OnEnterState and before every OnTick.
    class STAI_TaskBase : public STAI_NodeBase
    {
    public:
        // short name shown by the debug addon and the activity log: every task class has its own task_name
        static std::string task_name;

        virtual std::string GetName() const override
        {
            return task_name;
        }

        // called by the tree: track whether the task runs (STAI_TaskContext::bTaskActive) and turn its result into
        // the completion of its state (Running while the result does not complete it)
        EActionRunStatus EnterState(STAI_TaskContext& context) const;
        EActionRunStatus TickState(STAI_TaskContext& context, uint32 elapsed, bool minimal) const;
        void ExitState(STAI_TaskContext& context) const;
        void StateCompleted(STAI_TaskContext& context, EActionRunStatus status) const;

        // <Task completion="...">, else GetDefaultCompletion
        ESTAI_TaskCompletion GetCompletion() const { return completion ? *completion : GetDefaultCompletion(); }
        void SetCompletion(ESTAI_TaskCompletion value) { completion = value; }
        virtual ESTAI_TaskCompletion GetDefaultCompletion() const { return ESTAI_TaskCompletion::AlwaysFinish; }

        // OnTick is called only for tasks that tick (RedStateTree's bShouldCallTick)
        virtual bool ShouldCallTick() const { return false; }

        // the state runs a tree stacked on top of the state's tree (Task_RunSubtree): while it does, the state's tree
        // only ticks its active states
        virtual bool HoldsActiveTree(const STAI_TaskContext& context) const { return false; }

        // tree files this task runs (relative to the config directory), checked when the trees are loaded
        virtual std::vector<std::string> GetReferencedTrees() const { return {}; }
        // links the loaded trees of GetReferencedTrees into the task, once at server start when every tree is loaded
        // (find has each of them: trees with a missing subtree are dropped before)
        virtual void LinkReferencedTrees(const std::function<std::shared_ptr<const STAI_TreeAsset>(const std::string& file)>& find) {}
        // the linked tree the task runs (Task_RunSubtree), nullptr when none: the debug views show its states
        virtual const STAI_TreeAsset* GetLinkedTree() const { return nullptr; }

        // the task's runs get History entries; <Task class="..." show_log="false"> switches them off (e.g. an idle
        // Task_Wait that only waits for transitions), data providers and subtrees have none by default
        bool LogsActivity() const { return bShowLog; }
        void SetShowLog(bool show) { bShowLog = show; }

        // a run right after a run of the same task (same info, same state) is merged into its History entry (one
        // entry counting the runs, e.g. Task_Wait); false gives every run its own entry (each cast of Task_CastSpell)
        virtual bool AreConsecutiveLogsStacked() const { return true; }

        // debug visualization
        // the name of a run's History entry: GetDebugInfo, or what the run does (Task_CastSpell: the rank it casts)
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const { return GetDebugInfo(); }
        // adds the trees this task stacked on its state's tree (a running subtree)
        virtual void AppendDebugTreeStack(const STAI_TaskContext& context, std::vector<StateNodeDebugTree>& stack) const {}
        // the tree the task runs on top of this one (Task_RunSubtree), nullptr when none
        virtual class STAI_TreeInstance* GetRunningSubtree(STAI_TaskContext& context) const { return nullptr; }

    protected:
        // the node's hooks (RedStateTree's OnEnterState / OnTick / OnExitState / OnStateCompleted)
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const { return EActionRunStatus::Succeeded; }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const { return EActionRunStatus::Running; }
        virtual void OnExitState(STAI_TaskContext& context) const {}
        virtual void OnStateCompleted(STAI_TaskContext& context, EActionRunStatus status) const {}

        // History entries (activity log): tasks log their runs by themselves, one entry per run named by its
        // GetActivityInfo (else GetName), its source is the task's name. StartLog in OnEnterState remembers when the
        // run started; LogStatus with a status of the run: Running gives the run its entry (from its start),
        // Succeeded / Failed end it (a run that succeeds at once leaves none, a failed one always gets one); EndLog
        // ends it when the run is left while it runs, with whether it went well. Nothing is logged when LogsActivity
        // is off.
        void StartLog(STAI_TaskContext& context) const;
        EActionRunStatus LogStatus(STAI_TaskContext& context, EActionRunStatus status) const;
        void EndLog(STAI_TaskContext& context, bool successful) const;

        bool bShowLog = true;

    private:
        std::optional<ESTAI_TaskCompletion> completion;
    };

    // the name tasks were written against before the RedStateTree-like base classes
    using STAI_Task = STAI_TaskBase;
}
