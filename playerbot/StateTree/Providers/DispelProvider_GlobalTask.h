#pragma once

#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Providers/STAI_GroupAuraWatch.h"

#include <string>
#include <vector>

namespace ai
{
    class DispelProvider_GlobalTask;

    // runtime context of DispelProvider_GlobalTask (and CurseProvider_GlobalTask): the bot's subscriptions and who
    // carries a harmful aura of the watched dispel types right now
    struct DispelProvider_GlobalTaskContext : public STAI_GroupAuraWatchContext
    {
        // fired whenever an evaluation finds a member to dispel:
        //   <Transition type="OnDelegate" source="Root|DispelProvider_GlobalTask::onNeedsHelp" node="..."/>
        FSTAI_Delegate onNeedsHelp;
        // the members (self included) with any watched aura, the bot first; refreshed on every aura / group event:
        //   <Binding local="whoNeeds" external="Root|DispelProvider_GlobalTask::whoNeeds"/>
        std::vector<ObjectGuid> whoNeeds;
        // the same by dispel type (empty for types that are not watched), for one dispel spell per type:
        //   <Binding local="whoNeeds" external="Root|DispelProvider_GlobalTask::disease"/>
        std::vector<ObjectGuid> magic;
        std::vector<ObjectGuid> curse;
        std::vector<ObjectGuid> disease;
        std::vector<ObjectGuid> poison;

        const DispelProvider_GlobalTask* task = nullptr;

        virtual void Evaluate(Group* leftGroup = nullptr) override;
        virtual bool IsWatchedAura(const SpellAuraHolder* holder) const override;
    };

    // Global task that watches the bot and its group for harmful auras a dispel removes: property whoNeeds (every
    // member with one), magic / curse / disease / poison (by type), delegate onNeedsHelp (fired when an evaluation
    // finds any). Not tickable, like BuffProvider_GlobalTask.
    //   <Global>
    //     <Task class="DispelProvider_GlobalTask">
    //       <Dispel type="magic"/>      one per watched type: magic, curse, disease, poison
    //       <Dispel type="disease"/>
    //       <Aura spellid="..."/>       optional: only these auras (any rank), of the watched types
    //     </Task>
    //   </Global>
    // Task_CastSpell casts on them (a type's list bound into whoNeeds), e.g. Dispel Magic on magic.
    class DispelProvider_GlobalTask : public STAI_GlobalTask, public ISTAI_DebugSpells
    {
    public:
        DispelProvider_GlobalTask(uint32 dispelMask, std::vector<uint32> auras) : dispelMask(dispelMask), auras(std::move(auras)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "DispelProvider_GlobalTask"; }
        inline static std::string task_name = "DispelProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<DispelProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override { return auras; }

        // a harmful aura of a watched dispel type (and one of the configured auras, when there are some)
        bool IsWatchedHolder(const SpellAuraHolder* holder) const;
        // the watched dispel types of the unit's harmful auras (1 << DISPEL_*)
        uint32 GetWatchedDispels(const Unit* unit) const;

    protected:
        // <Dispel type="..."/> as a mask of 1 << DISPEL_*; fallback when none is configured
        static uint32 ParseDispelMask(const STAI_TaskConfig& config, uint32 fallback);
        static std::vector<uint32> ParseAuras(const STAI_TaskConfig& config);

    private:
        uint32 dispelMask;          // 1 << DISPEL_* of the watched types
        std::vector<uint32> auras;  // first ranks; empty: every aura of the watched types
    };

    // The DispelProvider for curses only, for the trees that only decurse (Mage):
    //   <Task class="CurseProvider_GlobalTask"/>               every dispellable curse
    //   <Task class="CurseProvider_GlobalTask">
    //     <Aura spellid="..."/>                                  only these auras (any rank)
    //   </Task>
    // Properties and delegate as DispelProvider_GlobalTask (whoNeeds, curse, onNeedsHelp).
    class CurseProvider_GlobalTask : public DispelProvider_GlobalTask
    {
    public:
        using DispelProvider_GlobalTask::DispelProvider_GlobalTask;

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "CurseProvider_GlobalTask"; }
        inline static std::string task_name = "CurseProvider";
        virtual std::string GetName() const override { return task_name; }
    };
}
