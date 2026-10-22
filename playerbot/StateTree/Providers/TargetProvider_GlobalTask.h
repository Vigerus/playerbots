#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Player;
class Unit;

namespace ai
{
    template<class T> class Value;

    // runtime context of TargetProvider_GlobalTask: the bot's target
    struct TargetProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when target changed (also to none):
        //   <Transition type="OnDelegate" source="Root|TargetProvider_GlobalTask::onTargetChanged" node="..."/>
        FSTAI_Delegate onTargetChanged;
        // the unit to attack, empty when there is none (and, with WithThreat, while threatHigh):
        //   <Condition binding="Root|TargetProvider_GlobalTask::target"/>
        //   <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
        ObjectGuid target;
        // WithThreat: the bot's threat on the chosen unit reached ThreatAmount of the highest other threat on it
        bool threatHigh = false;

        ObjectGuid chosen;          // the chosen unit, also while target is held back by threat
        bool marked = false;        // chosen is an rti mark (not the fallback)

        uint32 sinceCheckMs = 0;

        // the bot's "rti" / "rti cc" values, looked up once on enter
        Value<std::vector<std::string>>* rtiValue = nullptr;
        Value<std::vector<std::string>>* rtiCcValue = nullptr;
    };

    // Global task that picks the unit the bot attacks:
    //   <Global> <Task class="TargetProvider_GlobalTask"/> </Global>
    // 1. the group's marks in the order of the bot's "rti" icons (default skull): the first marked unit that is alive,
    //    hostile and in sight;
    // 2. without a living mark, the dps assist choice: among the bot's "possible attack targets" (units fighting the
    //    bot, its group or master) the one with the least health, players excluded.
    // Crowd-controlled units (polymorph, frost, sap, ...) and the bot's "rti cc" marks are never picked, so the
    // crowd control does not break. Ticked: the current target is checked every update (it is dropped as soon as it
    // dies or gets crowd-controlled), a new choice is made every 250ms; the fallback target is kept while it stays
    // valid (no switching mid-cast whenever health values change), a living mark always takes over.
    // Optional threat check (off by default):
    //   <Task class="TargetProvider_GlobalTask">
    //     <WithThreat value="true"/>     hold the target back while the bot nears aggro
    //     <ThreatAmount value="90"/>     percent of the highest other threat on the unit (default 90)
    //   </Task>
    // With a living tank in the group, a creature whose threat list has the bot at ThreatAmount or more of the
    // highest other entry (the tank, usually; also before anybody else has threat) is not published: target is empty
    // and onTargetChanged fires, so dps nodes are not selected; it is published again (onTargetChanged) once the
    // bot's share drops below. The legacy "current target" keeps the chosen unit.
    class TargetProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        TargetProvider_GlobalTask(bool withThreat, float threatShare) : withThreat(withThreat), threatShare(threatShare) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "TargetProvider_GlobalTask"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TargetProvider_GlobalTaskContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;


    private:
        // alive, hostile, in sight, not crowd-controlled, not one of the bot's crowd-control marks (ccMarks)
        static bool IsValidTarget(const STAI_TaskContext& context, Unit* unit, const std::vector<Unit*>& ccMarks);
        static Unit* FindMarkedTarget(const TargetProvider_GlobalTaskContext& context, const std::vector<Unit*>& ccMarks);
        static Unit* FindAssistTarget(const STAI_TaskContext& context, const std::vector<Unit*>& ccMarks);
        // the bot's threat on unit is at threatShare or more of the highest other threat (with a living tank around)
        static bool IsThreatHigh(const STAI_TaskContext& context, Unit* unit, float threatShare);
        static void SetChosen(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked);
        static void Choose(TargetProvider_GlobalTaskContext& context, bool choose);
        // publishes chosen as target unless the threat holds it back, fires onTargetChanged when target changed
        void Publish(TargetProvider_GlobalTaskContext& context, bool checkThreat) const;
        void Update(TargetProvider_GlobalTaskContext& context, bool choose) const;

        bool withThreat;
        float threatShare;          // ThreatAmount / 100
    };
}
