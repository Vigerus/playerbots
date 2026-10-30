#pragma once

#include "Platform/Define.h"

#include <string>

namespace ai
{
    // result of a task's update: Running keeps it running, Succeeded / Failed finish it
    enum class EActionRunStatus : uint8_t
    {
        Running = 0,
        Succeeded = 1,
        Failed = 2
    };

    // how a finished task completes its state (RedStateTree's ETaskType), <Task completion="...">
    enum class ESTAI_TaskCompletion : uint8
    {
        AlwaysFinish,           // the task finishing completes the state with its result
        FinishWhenSucceeded,
        FinishWhenFailed,
        IgnoreResult            // the state keeps running, the task stays finished
    };

    // <State type="...">
    enum class ESTAI_StateType : uint8
    {
        State,
        Subtree,                // only entered through a Linked state or a transition
        Linked                  // runs the Subtree state named by linkedSubtree in its place
    };

    // <State selection="...">: how a child is selected when the state is entered
    enum class ESTAI_StateSelection : uint8
    {
        TrySelectChildrenInOrder,
        TrySelectChildrenAtRandom,
        TrySelectChildrenWithHighestUtility,
        TrySelectChildrenAtRandomWeightedByUtility,
        TryFollowTransitions
    };

    // <Transition trigger="...">
    enum class ESTAI_TransitionTrigger : uint8
    {
        OnStateCompleted,
        OnStateSucceeded,
        OnStateFailed,
        OnTick,
        OnEvent,
        OnDelegate
    };

    // <Transition priority="...">: of the transitions triggered in one update the highest one is taken
    enum class ESTAI_TransitionPriority : uint8
    {
        Low,
        Normal,
        Medium,
        High,
        Critical
    };

    // the XML names of the enums; Parse returns false for an unknown name
    const char* ToString(ESTAI_TaskCompletion value);
    const char* ToString(ESTAI_StateType value);
    const char* ToString(ESTAI_StateSelection value);
    const char* ToString(ESTAI_TransitionTrigger value);
    const char* ToString(ESTAI_TransitionPriority value);
    bool Parse(const std::string& text, ESTAI_TaskCompletion& value);
    bool Parse(const std::string& text, ESTAI_StateType& value);
    bool Parse(const std::string& text, ESTAI_StateSelection& value);
    bool Parse(const std::string& text, ESTAI_TransitionTrigger& value);
    bool Parse(const std::string& text, ESTAI_TransitionPriority& value);
}
