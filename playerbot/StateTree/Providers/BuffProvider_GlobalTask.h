#pragma once

#include "playerbot/StateTree/Providers/STAI_BuffTypes.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Providers/STAI_GroupAuraWatch.h"

#include <string>
#include <vector>

namespace ai
{
    class BuffProvider_GlobalTask;

    // runtime context of BuffProvider_GlobalTask: the bot's subscriptions and the buffs missing right now
    struct BuffProvider_GlobalTaskContext : public STAI_GroupAuraWatchContext
    {
        // fired whenever an evaluation finds missing buffs:
        //   <Transition type="OnDelegate" source="Root|BuffProvider_GlobalTask::onMissingBuff" node="..."/>
        FSTAI_Delegate onMissingBuff;
        // casts needed, in config order; refreshed on every aura / group event:
        //   <Binding local="needs" external="Root|BuffProvider_GlobalTask::needs"/>
        std::vector<BuffNeed> needs;

        const BuffProvider_GlobalTask* task = nullptr;

        virtual void Evaluate(Group* leftGroup = nullptr) override;
        virtual bool IsWatchedAura(const SpellAuraHolder* holder) const override;
    };

    // Global task that watches the buffs of the bot and its group: property needs (the casts to do), delegate
    // onMissingBuff (fired when an evaluation finds any). Not tickable: it reacts to the TeamSubsystem (group
    // changes) and SpellAuraWorldSubsystem (auras of every member, self included) events. Task_CastBuff casts
    // what it found (needs bound into it).
    //   <Global>
    //     <Task class="BuffProvider_GlobalTask">
    //       <Buff spellid="1459"/>                 Arcane Intellect, any rank of the spell (the bot casts its highest)
    //       <Buff spellid="23028"/>                Arcane Brilliance: same aura as Arcane Intellect, so the same buff
    //       <Buff spellid="6117" scope="self"/>    scope optional: self, single or group (default: from the spell)
    //     </Task>
    //   </Global>
    // Spells applying the same aura (type and misc value of their first aura effect) are one buff: a member has
    // it when any rank of any of them is on it. Self spells are only checked on the bot. Otherwise a group spell is
    // cast when 2+ members of a subgroup miss the buff, a single target spell on a member missing it alone (a group
    // spell when the bot knows no single target one).
    class BuffProvider_GlobalTask : public STAI_GlobalTask, public ISTAI_DebugSpells
    {
    public:
        struct BuffSpell
        {
            uint32 firstSpellId = 0;    // first rank, every rank counts
            BuffScope scope = BuffScope::Single;
        };

        // spells providing the same buff
        struct Buff
        {
            uint32 auraType = 0;
            int32 miscValue = 0;
            std::vector<BuffSpell> spells;
        };

        explicit BuffProvider_GlobalTask(std::vector<Buff> buffs) : buffs(std::move(buffs)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "BuffProvider_GlobalTask"; }
        inline static std::string task_name = "BuffProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<BuffProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

        // the casts the bot should do now for the reachable members (the bot included)
        std::vector<BuffNeed> FindNeeds(Player* bot, Group* group, const std::vector<Player*>& members) const;
        // the spell is a rank of one of the configured spells
        bool IsWatchedSpell(uint32 spellId) const;

    private:
        static std::vector<Buff> ParseBuffs(const STAI_TaskConfig& config);
        static bool HasBuff(const Unit* unit, const Buff& buff);
        // highest rank of the spell the bot knows, 0 when none
        static uint32 GetKnownRank(const Player* bot, uint32 firstSpellId);

        std::vector<Buff> buffs;
    };
}
