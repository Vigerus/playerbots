#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/STAI_Navigation.h"

#include <vector>

class DynamicObject;
class Player;

namespace ai
{
    // runtime context of HazardProvider_GlobalTask: whether the bot stands in a hostile area effect and where to go
    struct HazardProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the bot got into a hazard, and every second while it is still in one:
        //   <Transition type="OnDelegate" source="Root|HazardProvider_GlobalTask::onHazard" node="..."/>
        FSTAI_Delegate onHazard;
        // the bot stands in a hostile area effect
        bool inHazard = false;
        // the closest spot outside every hazard around the bot, for Task_MoveToPosition; unset outside hazards or
        // when no spot was found:
        //   <Binding local="position" external="Root|HazardProvider_GlobalTask::safePosition"/>
        STAI_Position safePosition;

        uint32 sinceCheckMs = 0;
        uint32 sinceFireMs = 0;
    };

    // Global task that watches the area effects on the ground around the bot (dynamic objects: Rain of Fire,
    // Consecration, Blizzard, poison clouds, ...) cast by units hostile to it with a harmful effect:
    //   <Global>
    //     <Task class="HazardProvider_GlobalTask">
    //       <Margin value="2"/>      yards a safe spot keeps from the edge of a hazard (default 2)
    //     </Task>
    //   </Global>
    // Ticked: checks every 250ms. inHazard while the bot is inside one (or affected by it); safePosition is the closest
    // of the spots around the bot (16 directions, up to 20 yards) outside every hazard by the margin, in sight and
    // on ground the bot can stand on, kept while it stays safe.
    class HazardProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        explicit HazardProvider_GlobalTask(float margin) : margin(margin) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "HazardProvider_GlobalTask"; }
        inline static std::string task_name = "HazardProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<HazardProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static float ParseMargin(const STAI_TaskConfig& config);
        // the hostile, harmful area effects near the bot
        static std::vector<DynamicObject*> FindHazards(Player* bot);
        // the spot is outside every hazard by the margin
        bool IsSafe(float x, float y, const std::vector<DynamicObject*>& hazards) const;
        STAI_Position FindSafePosition(Player* bot, const std::vector<DynamicObject*>& hazards) const;
        void Update(HazardProvider_GlobalTaskContext& context) const;

        float margin;   // yards
    };
}
