#include "StateNode.h"

#include "Tasks/StateTreeTask.h"

#include "playerbot/strategy/Event.h"

void StateNode::Update(uint32 elapsed, bool minimal)
{
    ai::Event ev;
    if (runningTask)
    {
        EActionRunStatus status = runningTask->Tick(runningCtx, ev);
        if (status != EActionRunStatus::Running)
        {
            runningTask->Exit(runningCtx, ev);
            runningTask.reset();
        }
        return;
    }

    // evaluate utilities
    float best = -FLT_MAX;
    std::unique_ptr<StateTreeTask> bestTask;
    ActionRunContext bestCtx;
    for (auto& f : factories)
    {
        auto candidate = f();
        ActionRunContext ctx;
        ctx.relevance = candidate->ComputeUtility(ctx);
        float u = ctx.relevance; // optionally add state multipliers
        if (u > best)
        {
            best = u;
            bestTask = std::move(candidate);
            bestCtx = ctx;
        }
    }

    if (bestTask && best > 0.0f)
    {
        runningTask = std::move(bestTask);
        runningCtx = bestCtx;
        EActionRunStatus st = runningTask->Enter(runningCtx, ev);
        if (st == EActionRunStatus::Running)
        {
            // keep as running
        }
        else
        {
            runningTask->Exit(runningCtx, ev);
            runningTask.reset();
        }
    }
}