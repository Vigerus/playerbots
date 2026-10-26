#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Tasks/STAI_SpellCause.h"
#include "playerbot/StateTree/Tasks/STAI_SpellConfig.h"

#include "Entities/ObjectGuid.h"

#include <vector>

class Unit;

namespace ai
{
    // runtime context of InterruptProvider_GlobalTask: the casters to interrupt right now
    struct InterruptProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when a cast to interrupt shows up (another caster first, or a new cast of it):
        //   <Transition type="OnDelegate" source="Root|InterruptProvider_GlobalTask::onInterruptNeeded" node="..."/>
        FSTAI_Delegate onInterruptNeeded;
        // the casters the bot can interrupt now, healing casts first, for Task_CastSpell:
        //   <Binding local="whoNeeds" external="Root|InterruptProvider_GlobalTask::whoNeeds"/>
        std::vector<ObjectGuid> whoNeeds;
        // per caster of whoNeeds, the spell it casts, for the debug view of the interrupt:
        //   <Binding local="causes" external="Root|InterruptProvider_GlobalTask::causes"/>
        STAI_SpellCauses causes;

        uint32 sinceCheckMs = 0;
        // the first cast of whoNeeds when it was last announced
        ObjectGuid announcedCaster;
        uint32 announcedSpell = 0;
    };

    // Global task that watches the enemies of the bot and its group (the units on their threat lists) for casts its
    // interrupt can stop:
    //   <Global>
    //     <Task class="InterruptProvider_GlobalTask" spellid="Counterspell">    the bot's interrupt (id or name)
    //       <Observe spellid="8399,12345"/>    optional: only casts of these spells (any rank); spellid="": none
    //       <Ignore spellid="8399,12345"/>     optional, not with <Observe>: casts of these spells are left alone
    //     </Task>
    //   </Global>
    // A unit needs interrupting while it casts or channels an interruptible spell (Spell::IsInterruptible) and the
    // bot can cast its interrupt on it now (known, off cooldown, in range and sight, mana): nothing is published while
    // the interrupt is on cooldown. Ticked: checks every 100ms; onInterruptNeeded fires when the first caster of
    // whoNeeds or its spell changes. Property causes (the spell of each caster) bound into Task_CastSpell's causes
    // shows what the interrupt stops.
    class InterruptProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        InterruptProvider_GlobalTask(STAI_ConfiguredSpell spell, STAI_SpellFilter filter) : spell(std::move(spell)), filter(std::move(filter)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "InterruptProvider_GlobalTask"; }
        inline static std::string task_name = "InterruptProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<InterruptProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::string GetDebugInfo() const override { return "Interrupt with " + STAI_SpellMarkup(spell.spellId); }

    private:
        // the interruptible spell the unit casts or channels, one the filter allows; nullptr when none
        const SpellEntry* GetInterruptibleCast(Unit* unit) const;
        void Update(InterruptProvider_GlobalTaskContext& context) const;

        STAI_ConfiguredSpell spell;     // the bot's interrupt
        STAI_SpellFilter filter;        // <Observe> / <Ignore>
    };
}
