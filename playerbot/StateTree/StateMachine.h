#pragma once

#include "HLS.h"

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

    private:
        log4cxx::LoggerPtr m_logger;

        bool bInitialized = false;

        IStateNode* FindNode(HLSStateType type) const;
    };

}