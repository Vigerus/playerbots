#pragma once
#include "strategy/Event.h"
#include <typeindex>

namespace ai
{
    enum class EActionRunStatus : uint8_t
    {
        Running = 0,
        Succeeded = 1,
        Failed = 2
    };

    struct ActionRunContext
    {
        std::string name;
        float relevance = 0.0f;
        // Add other fields (owner, timestamps, debug info) as needed
    };

    class StateTreeTask
    {
    public:
        virtual ~StateTreeTask() = default;

        // identify context type (for typed contexts later)
        virtual std::type_index GetActionContextClass() const = 0;

        // lifecycle
        virtual EActionRunStatus Enter(ActionRunContext& ctx, ai::Event& event) = 0;
        virtual EActionRunStatus Tick(ActionRunContext& ctx, ai::Event& event) = 0;
        virtual void Exit(ActionRunContext& ctx, ai::Event& event) = 0;

        // introspection
        virtual float ComputeUtility(const ActionRunContext& ctx) const { return ctx.relevance; }
        virtual bool IsReaction() const { return false; }
    };

}