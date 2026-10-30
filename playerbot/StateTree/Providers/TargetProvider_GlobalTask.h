#pragma once

#include "playerbot/StateTree/Providers/STAI_TargetAuraWatch.h"
#include "playerbot/StateTree/Providers/STAI_TargetIcons.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <map>
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
        // per <Aura spellid="..."/>: its stack on target (any rank, any caster), 0 without it, read by key:
        //   <Condition binding="Root|TargetProvider_GlobalTask::auraStacks" key="7386" compare="lesser" value="5"/>
        std::map<uint32, uint32> auraStacks;
        // fired when an entry of auraStacks changed (also by a target change):
        //   <Transition type="OnDelegate" source="Root|TargetProvider_GlobalTask::onAuraChanged" node="..."/>
        FSTAI_Delegate onAuraChanged;
        // TankRange: a living tank of the group is within TankRange of the chosen unit, or has been since it was
        // chosen (true without TankRange, without a group or without another tank in it)
        bool tankNear = true;
        // fired when tankNear turned true:
        //   <Transition type="OnDelegate" source="Root|TargetProvider_GlobalTask::onTankNear" node="..."/>
        FSTAI_Delegate onTankNear;

        ObjectGuid chosen;          // the chosen unit, also while target is held back by threat
        bool marked = false;        // chosen is an rti mark (not the fallback)
        uint32 iconsVersion = 0;    // PlayerbotTargetIcons::GetVersion of the last choice: a change chooses again
        ObjectGuid tankNearUnit;    // the chosen unit a tank has come near (tankNear stays true while it is chosen)

        STAI_TargetAuraWatch auraWatch;

        uint32 sinceCheckMs = 0;
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
    //     <AssistTank value="true"/>     without a mark, the unit a living tank of the group attacks first (melee:
    //                                    the bots gather on the tank's unit instead of the weakest one), default false
    //     <TankRange value="10"/>        tankNear: a tank has come within 10 yards of the chosen unit (melee dps waits
    //                                    while the tank pulls the unit to itself), off by default (tankNear true)
    //     <Aura spellid="7386"/>         auraStacks[7386]: the stack of the aura on target (Sunder Armor), per aura
    //   </Task>
    // The watched auras are an internal subscription (STAI_TargetAuraWatch): the target's aura events of the
    // SpellAuraWorldSubsystem, moved along with the target.
    // With a living tank in the group, a creature whose threat list has the bot at ThreatAmount or more of the
    // highest other entry (the tank, usually; also before anybody else has threat) is not published: target is empty
    // and onTargetChanged fires, so dps nodes are not selected; it is published again (onTargetChanged) once the
    // bot's share drops below. The legacy "current target" keeps the chosen unit.
    class TargetProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        TargetProvider_GlobalTask(bool withThreat, float threatShare, bool assistTank, float tankRange, std::vector<STAI_WatchedAura> auras)
            : withThreat(withThreat), threatShare(threatShare), assistTank(assistTank), tankRange(tankRange), auras(std::move(auras)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "TargetProvider_GlobalTask"; }
        inline static std::string task_name = "TargetProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TargetProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;


    private:
        // alive, hostile, in sight, not crowd-controlled, not one of the bot's crowd-control marks (ccMarks)
        static bool IsValidTarget(const STAI_TaskContext& context, Unit* unit, const std::vector<Unit*>& ccMarks);
        static Unit* FindMarkedTarget(const TargetProvider_GlobalTaskContext& context, const std::vector<Unit*>& ccMarks);
        static Unit* FindAssistTarget(const STAI_TaskContext& context, const std::vector<Unit*>& ccMarks, bool assistTank);
        // the bot's threat on unit is at threatShare or more of the highest other threat (with a living tank around)
        static bool IsThreatHigh(const STAI_TaskContext& context, Unit* unit, float threatShare);
        static void SetChosen(TargetProvider_GlobalTaskContext& context, Unit* unit, bool marked);
        void Choose(TargetProvider_GlobalTaskContext& context, bool choose) const;
        // publishes chosen as target unless the threat holds it back, fires onTargetChanged when target changed
        void Publish(TargetProvider_GlobalTaskContext& context, bool checkThreat) const;
        void Update(TargetProvider_GlobalTaskContext& context, bool choose) const;
        // a living tank of the group other than the bot is within range of unit, or the group has none
        static bool IsTankNear(const STAI_TaskContext& context, Unit* unit, float range);
        void UpdateTankNear(TargetProvider_GlobalTaskContext& context, bool check) const;
        void UpdateAuras(TargetProvider_GlobalTaskContext& context) const;

        bool withThreat;
        float threatShare;          // ThreatAmount / 100
        bool assistTank;
        float tankRange;            // 0: tankNear is always true
        std::vector<STAI_WatchedAura> auras;
    };
}
