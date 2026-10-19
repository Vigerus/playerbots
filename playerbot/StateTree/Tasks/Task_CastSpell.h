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
    class Task_CastSpell : public STAI_Task
    {
    public:
        Task_CastSpell(uint32 spellId, const std::string& spellName, uint32 displaySpellId)
            : spellId(spellId), spellName(spellName), displaySpellId(displaySpellId) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_CastSpell"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_CastSpellContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;
        // the spell being cast, else the bot's rank of it, else the configured one
        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

    private:
        // the spell the bot casts: the configured id when it knows it, else its highest rank of the configured name
        uint32 ResolveSpell(const Player* bot) const;

        uint32 spellId;             // 0: by name
        std::string spellName;      // lower case
        uint32 displaySpellId;      // a spell of that id / name, for the debug view
    };
}
