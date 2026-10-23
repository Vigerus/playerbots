#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"

#include <vector>

namespace ai
{
    // runtime context of Task_SelfResurrect: when it asked for the self resurrection
    struct Task_SelfResurrectContext : public STAI_TaskContext
    {
        uint32 spellId = 0;         // the self resurrection used, 0 none
        uint32 sinceUseMs = 0;
    };

    // Comes back by itself, dead, like the client's "Use Reincarnation / Soulstone" button (CMSG_SELF_RES): the core sets
    // the bot's self resurrection spell when it dies (PLAYER_SELF_RES_SPELL: Reincarnation while the shaman knows it, it
    // is off cooldown and an Ankh is in the bags; a Soulstone's resurrection):
    //   <Task class="Task_SelfResurrect">
    //     <Spell id="21169"/>   only these self resurrections (any rank), optional: every one without
    //   </Task>
    // The spell must be off cooldown and its reagents in the bags. Succeeded once the bot is alive (also when it already
    // is), Failed when it has no allowed self resurrection or it did not bring the bot back within 5s.
    class Task_SelfResurrect : public STAI_Task, public ISTAI_DebugSpells
    {
    public:
        explicit Task_SelfResurrect(std::vector<uint32> spells) : spells(std::move(spells)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_SelfResurrect"; }
        inline static std::string task_name = "SelfResurrect";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_SelfResurrectContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override { return "Self resurrection"; }
        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

    private:
        static std::vector<uint32> ParseSpells(const STAI_TaskConfig& config);

        std::vector<uint32> spells;     // first ranks of the allowed self resurrections, empty: every one
    };
}
