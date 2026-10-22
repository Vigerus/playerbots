#pragma once
#include "StateTreeTask.h"

class StateTreeTask_LegacyAction : public ActionTask
{
    Action* legacy; // pointer returned from InitializeAction, not owned
public:
    StateTreeTask_LegacyAction(Action* a) : legacy(a) {}
    std::type_index GetActionContextClass() const override { return typeid(ActionRunContext); }
    EActionRunStatus Enter(ActionRunContext& ctx, Event& ev) override
    {
        bool ok = legacy->Execute(const_cast<Event&>(ev)); // existing API
        return ok ? EActionRunStatus::Succeeded : EActionRunStatus::Failed;
    }
    EActionRunStatus Tick(ActionRunContext&, Event&) override { return EActionRunStatus::Succeeded; }
    void Exit(ActionRunContext&, Event&) override {}
    float ComputeUtility(const ActionRunContext& ctx) const override { return legacy->getRelevance(); }
    bool IsReaction() const override { return legacy->IsReaction(); }
};