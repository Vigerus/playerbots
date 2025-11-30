#pragma once

#include "HLS.h"

namespace ai
{

    class StateNode_Legacy : public IStateNode
    {
    public:
        StateNode_Legacy(PlayerbotAI* ai, Player* bot, const std::string& name, HLSStateType stateType) :
            ai(ai), bot(bot), name(name), stateType(stateType) {}

        virtual const char* GetName() const override { return name.c_str(); }

        virtual HLSStateType GetStateType() const override { return stateType; }

        virtual void OnEnter() override {}
        virtual void OnExit() override {}

        virtual void Update(uint32 elapsed, bool minimal) override;
    private:
        PlayerbotAI* ai;
        Player* bot;
        std::string name;
        HLSStateType stateType;
    };

}