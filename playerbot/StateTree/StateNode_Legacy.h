#pragma once

#include "HLS.h"

namespace ai
{
    class Engine;
    class ReactionEngine;

    class StateNode_Legacy : public IStateNode
    {
    public:
        StateNode_Legacy(PlayerbotAI* ai, Player* bot, const std::string& name, HLSStateType stateType) :
            ai(ai), bot(bot), name(name), stateType(stateType) {}

        virtual const char* GetName() const override { return name.c_str(); }

        virtual HLSStateType GetStateType() const override { return stateType; }

        // temporary, for compatibility
        virtual void UpdateStrategies(const std::string& name, BotState type) override;
        virtual void ResetStrategies() override;
        virtual void ReInit() override;


        virtual void OnEnter() override {}
        virtual void OnExit() override {}

        virtual void Update(uint32 elapsed, bool minimal) override;
    private:
        PlayerbotAI* ai;
        Player* bot;
        std::string name;
        HLSStateType stateType;
    };

    class StateNode_Reaction : public IStateNode, public IReactionChecker
    {
    public:
        StateNode_Reaction(PlayerbotAI* ai, Player* bot);

        virtual const char* GetName() const override { return "StateNode_Reaction"; }

        virtual HLSStateType GetStateType() const override { return HLSStateType::Reaction; }

        // temporary, for compatibility
        virtual void UpdateStrategies(const std::string& name, BotState type) override;
        virtual void ResetStrategies() override;
        virtual void ReInit() override;


        virtual void OnEnter() override {}
        virtual void OnExit() override;

        virtual void Update(uint32 elapsed, bool minimal) override;

        virtual bool ShouldEnterReaction() override;
        virtual bool IsReacting() override;
    private:
        PlayerbotAI* ai;
        Player* bot;

        bool UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned);
        
        std::unique_ptr<ReactionEngine> reactionEngine;
    };
}