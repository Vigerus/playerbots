#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <utility>
#include <vector>

namespace ai
{
    // A constant score:
    //   <Consideration class="Consideration_Constant" value="0.5"/>
    class Consideration_Constant : public STAI_ConsiderationBase
    {
    public:
        explicit Consideration_Constant(float value) : value(value) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConsiderationBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Consideration_Constant"; }
        virtual std::string GetDebugInfo() const override;

    protected:
        virtual float CalculateScore(STAI_NodeContext& context) const override { return value; }

    private:
        float value;
    };

    // A number property of a task mapped to a score (RedStateTree's Number consideration): normalized from
    // min..max into 0..1, then through a piecewise linear curve (none: the normalized number):
    //   <Consideration class="Consideration_Number" binding="Root|HealProvider_GlobalTask::lowestPct" min="0" max="100">
    //     <Key x="0" y="1"/>
    //     <Key x="1" y="0"/>
    //   </Consideration>
    class Consideration_Number : public STAI_ConsiderationBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_ConsiderationBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Consideration_Number"; }
        virtual std::string GetDebugInfo() const override;
        virtual void ValidatePropertyRef(size_t index, const STAI_PropertyDef& property) const override;

    protected:
        virtual float CalculateScore(STAI_NodeContext& context) const override;

    private:
        std::string binding;
        float min = 0.0f;
        float max = 1.0f;
        std::vector<std::pair<float, float>> keys;      // x, y by x
    };
}
