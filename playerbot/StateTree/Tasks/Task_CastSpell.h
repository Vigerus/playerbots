#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Tasks/STAI_SpellConfig.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of Task_CastSpell: the targets (bound) and the cast in progress
    struct Task_CastSpellContext : public STAI_TaskContext
    {
        ObjectGuid target;                  // property "target": one unit, e.g. bound to TargetProvider_GlobalTask::target
        std::vector<ObjectGuid> whoNeeds;   // property "whoNeeds": units on the bot's map, e.g. bound to CurseProvider_GlobalTask::whoNeeds
        uint32 spellId = 0;                 // 0: nothing was cast
        ObjectGuid castTarget;
        uint32 castStartMs = 0;
        bool resultAtStart = false;         // instant / channeled: successful once started
        int8 result = -1;                   // the spell went off: -1 not known yet, 0 no, 1 yes
    };

    // Casts a spell on its target, else on the first of its whoNeeds it can reach, then waits for the cast and the
    // global cooldown:
    //   <Task class="Task_CastSpell" spellid="Remove Lesser Curse">     spell id, or a name (the highest known rank)
    //     <Binding local="whoNeeds" external="Root|CurseProvider_GlobalTask::whoNeeds"/>
    //   </Task>
    //   <Task class="Task_CastSpell" spellid="Frostbolt">
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //   </Task>
    //   <Task class="Task_CastSpell" spellid="Ice Block">
    //     <Target value="self"/>      the bot itself, before target and whoNeeds
    //   </Task>
    //   <Task class="Task_CastSpell" spellid="Resurrection">
    //     <Target value="dead"/>      only dead targets (resurrections), instead of only living ones
    //     <Binding local="whoNeeds" external="Root|HealProvider_GlobalTask::dead"/>
    //   </Task>
    //   <Task class="Task_CastSpell" spellid="Power Word: Shield">
    //     <SkipAura spellid="6788"/>  targets with this aura (any rank, any caster) are skipped: Weakened Soul
    //     <SkipAura own="true"/>      targets with the cast spell's aura (any rank) from the bot are skipped: Renew
    //     <Binding local="whoNeeds" external="Root|HealProvider_GlobalTask::critical"/>
    //   </Task>
    //   <Task class="Task_CastSpell" spellid="Flash Heal">
    //     <Fallback spellid="Heal"/>  cast instead while the bot knows none of the spells before (low levels)
    //     <Fallback spellid="Lesser Heal"/>
    //   </Task>
    // Succeeded once the cast and the GCD are over, or in OnEnter when there is no living target; Failed in OnEnter
    // when the bot knows none of its spells, when every target is skipped by <SkipAura> (so the node waits its retry
    // delay instead of being selected again at once) or when it can cast on none of the living targets (range,
    // sight, mana...).
    // Its History entries ("Cast <spell>") are successful when the spell went off: instant and channeled spells (Fire
    // Blast, Blizzard, Arcane Missiles: no cast time / channel attributes) as soon as they started, spells with a
    // cast time when the server sent their spell go; a failure, no spell go, or a cast left unfinished (the node
    // left, the tree finished) is a failed run.
    class Task_CastSpell : public STAI_Task
    {
    public:
        // <SkipAura>: an aura the target must not have
        struct SkipAura
        {
            uint32 firstSpellId = 0;    // 0: the cast spell's own aura
            bool own = false;           // only the bot's aura counts
        };

        Task_CastSpell(STAI_ConfiguredSpell spell, bool self, bool dead, std::vector<SkipAura> skipAuras, std::vector<STAI_ConfiguredSpell> fallbacks)
            : spell(std::move(spell)), self(self), dead(dead), skipAuras(std::move(skipAuras)), fallbacks(std::move(fallbacks)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_CastSpell"; }
        inline static std::string task_name = "CastSpell";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_CastSpellContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Cast " + STAI_SpellMarkup(spell.spellId); }
        // every cast gets its own History entry
        virtual bool AreConsecutiveLogsStacked() const override { return false; }

    private:
        // the cast's spell go / failure since it started: 1 went off, 0 failed, -1 neither yet
        static int8 CastResult(const Task_CastSpellContext& context);
        static std::vector<SkipAura> ParseSkipAuras(const STAI_TaskConfig& config);
        static std::vector<STAI_ConfiguredSpell> ParseFallbacks(const STAI_TaskConfig& config);
        // the spell's rank the bot casts, else the first <Fallback> it knows; 0 when it knows none
        uint32 ResolveSpell(const Player* bot) const;
        // the target has one of the <SkipAura> auras (spellId: the rank the bot casts)
        bool IsSkipped(const Player* bot, const Unit* target, uint32 spellId) const;

        STAI_ConfiguredSpell spell;     // the spell the bot casts (its highest known rank when named)
        bool self;                      // <Target value="self"/>: the bot is the first target
        bool dead;                      // <Target value="dead"/>: only dead targets
        std::vector<SkipAura> skipAuras;
        std::vector<STAI_ConfiguredSpell> fallbacks;   // <Fallback spellid="..."/> in order
    };
}
