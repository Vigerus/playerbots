#pragma once

#include "HLS.h"
#include "STAI_ActivityLog.h"

#include "Log/LogHelper.h"

namespace ai
{
    class IReactionChecker
    {
        public:
            virtual bool ShouldEnterReaction() = 0;
            virtual bool IsReacting() = 0;
    };

    class StateMachine
    {
        IStateNode* active = nullptr;
        IStateNode* initial = nullptr;
        IStateNode* previous = nullptr;

        std::map<HLSStateType, std::unique_ptr<IStateNode>> nodes;
    public:
        StateMachine();

        void RegisterNode(std::unique_ptr<IStateNode> node, bool bInitial = false);

        void PreemptTo(HLSStateType next);
        void ResumePrevious();
        void TransitionTo(HLSStateType type);

        void Update(uint32 elapsed, bool minimal);

        // temporary, for compatibility
        void Reset();
        void ReInitCurrentEngine();
        void UpdateStrategies(const std::string& name, BotState type);
        void ResetStrategies();

        IStateNode* GetActive() const { return active; }
        // the state the bot is in: the active one, or during a reaction preempt the one the reaction resumes to
        bool GetStateType(HLSStateType& type) const;

        // serialized tree state for the client-side StateTree visualizer addon (see BotStateTree addon)
        std::string BuildDebugSnapshot() const;
        // serialized recent activity (history tab of the addon)
        std::string BuildDebugHistory();
        // the addon's task toggle: state type name, tree name, node and task index of the snapshot's top tree
        bool SetTaskDisabled(const std::string& state, const std::string& tree, int node, int task, bool disabled);

        STAI_ActivityLog& GetActivityLog() { return activityLog; }

    private:
        log4cxx::LoggerPtr m_logger;

        bool bInitialized = false;
        uint32 activeSinceMs = 0;
        STAI_ActivityLog activityLog;

        IStateNode* FindNode(HLSStateType type) const;
    };

}