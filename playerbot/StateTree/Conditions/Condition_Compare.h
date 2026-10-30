#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

namespace ai
{
    // A task property tested by path (RedStateTree's Compare):
    //   <Condition class="Condition_Compare" binding="Root|BuffProvider_GlobalTask::needs"/>
    //       passes while the property is set (a container not empty, otherwise its bool value)
    //   <Condition class="Condition_Compare" binding="Root|ManaProvider_GlobalTask::manaPct" compare="lesser" value="90"/>
    //       compares a number property (compare: equal, lesser, lesser or equal, greater, greater or equal)
    //   <Condition class="Condition_Compare" binding="Root|CooldownProvider_GlobalTask::ready" key="45438"/>
    //       the entry of a map of numbers (or bools) instead of the property itself: passes while it is set
    //       (non-zero), compare as above; a key the map does not have is not set (compared as 0)
    //   <Condition class="Condition_Compare" binding="Root|SpecProvider_GlobalTask::specName" compare="equal" value="frost mage"/>
    //       a text property equal to the value (only compare="equal")
    class Condition_Compare : public STAI_ConditionBase
    {
    public:
        enum class Compare : uint8 { None, Equal, Lesser, LesserOrEqual, Greater, GreaterOrEqual };

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConditionBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Condition_Compare"; }
        virtual std::string GetDebugInfo() const override;
        virtual void ValidatePropertyRef(size_t index, const STAI_PropertyDef& property) const override;

    protected:
        virtual bool OnTestCondition(STAI_NodeContext& context) const override;

    private:
        std::string binding;
        Compare compare = Compare::None;
        std::string compareText;
        double value = 0.0;
        bool hasKey = false;        // key="...": an entry of a map property
        double key = 0.0;
        bool isText = false;        // compare="equal" with a value that is no number: compares a text property
        std::string text;
    };
}
