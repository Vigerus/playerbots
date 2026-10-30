#include "STAI_Types.h"

using namespace ai;

namespace
{
    const char* const completionNames[] = { "AlwaysFinish", "FinishWhenSucceeded", "FinishWhenFailed", "IgnoreResult" };
    const char* const stateTypeNames[] = { "State", "Subtree", "Linked" };
    const char* const selectionNames[] = { "TrySelectChildrenInOrder", "TrySelectChildrenAtRandom", "TrySelectChildrenWithHighestUtility",
        "TrySelectChildrenAtRandomWeightedByUtility", "TryFollowTransitions" };
    const char* const triggerNames[] = { "OnStateCompleted", "OnStateSucceeded", "OnStateFailed", "OnTick", "OnEvent", "OnDelegate" };
    const char* const priorityNames[] = { "Low", "Normal", "Medium", "High", "Critical" };

    template<class Enum, size_t Count>
    const char* NameOf(const char* const (&names)[Count], Enum value)
    {
        const size_t index = size_t(value);
        return index < Count ? names[index] : "?";
    }

    template<class Enum, size_t Count>
    bool ParseName(const char* const (&names)[Count], const std::string& text, Enum& value)
    {
        for (size_t i = 0; i < Count; ++i)
        {
            if (text == names[i])
            {
                value = Enum(i);
                return true;
            }
        }
        return false;
    }
}

namespace ai
{
    const char* ToString(ESTAI_TaskCompletion value) { return NameOf(completionNames, value); }
    const char* ToString(ESTAI_StateType value) { return NameOf(stateTypeNames, value); }
    const char* ToString(ESTAI_StateSelection value) { return NameOf(selectionNames, value); }
    const char* ToString(ESTAI_TransitionTrigger value) { return NameOf(triggerNames, value); }
    const char* ToString(ESTAI_TransitionPriority value) { return NameOf(priorityNames, value); }

    bool Parse(const std::string& text, ESTAI_TaskCompletion& value) { return ParseName(completionNames, text, value); }
    bool Parse(const std::string& text, ESTAI_StateType& value) { return ParseName(stateTypeNames, text, value); }
    bool Parse(const std::string& text, ESTAI_StateSelection& value) { return ParseName(selectionNames, text, value); }
    bool Parse(const std::string& text, ESTAI_TransitionTrigger& value) { return ParseName(triggerNames, text, value); }
    bool Parse(const std::string& text, ESTAI_TransitionPriority& value) { return ParseName(priorityNames, text, value); }
}
