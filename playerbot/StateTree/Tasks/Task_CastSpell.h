#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

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
        bool failed = false;
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
    // Succeeded once the cast and the GCD are over, or right away when there is no living target; Failed when the
    // bot does not know the spell or can cast it on none of the living targets (range, sight, mana...).
    // Its History entries ("Cast <spell>") are successful when the spell went off: instant and channeled spells (Fire
    // Blast, Blizzard, Arcane Missiles: no cast time / channel attributes) as soon as they started, spells with a
    // cast time when the server sent their spell go; a failure, no spell go, or a cast left unfinished (the node
    // left, the tree finished) is a failed run.
    class Task_CastSpell : public STAI_Task
    {
    public:
        Task_CastSpell(uint32 spellId, const std::string& spellName, std::vector<uint32> namedSpells = {})
            : spellId(spellId), spellName(spellName), namedSpells(std::move(namedSpells)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_CastSpell"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_CastSpellContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        // whether the spell went off (also when the run is left while the cast is still going: it did not)
        virtual bool IsSuccessful(const STAI_TaskContext& context, EActionRunStatus status) const override;
        virtual std::string GetDebugInfo() const override { return "Cast " + STAI_SpellMarkup(spellId); }
        // every cast gets its own History entry
        virtual bool AreConsecutiveLogsStacked() const override { return false; }

    private:
        // the spell the bot casts: the configured id when it knows it, else its highest rank of the configured name
        uint32 ResolveSpell(const Player* bot) const;
        // the cast's spell go / failure since it started: 1 went off, 0 failed, -1 neither yet
        static int8 CastResult(const Task_CastSpellContext& context);

        uint32 spellId;             // the configured spell id, or the first spell of the configured name
        std::string spellName;      // lower case, empty when configured by id: by name the bot's highest rank is cast
        std::vector<uint32> namedSpells;    // by name: every spell of that name, highest rank first (found once at load)
    };
}
