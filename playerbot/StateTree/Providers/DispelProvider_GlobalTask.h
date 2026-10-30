#pragma once

#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Tasks/STAI_SpellCause.h"
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
        // per member and watched type, its first watched aura, for the debug view of the dispel:
        //   <Binding local="causes" external="Root|DispelProvider_GlobalTask::causes"/>
        STAI_SpellCauses causes;

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
    //       <Observe spellid="8399,12345"/>  optional: only these auras (any rank); spellid="": none
    //       <Ignore spellid="8399,12345"/>   optional, not with <Observe>: never these auras (any rank)
    //     </Task>
    //   </Global>
    // Task_CastSpell casts on them (a type's list bound into whoNeeds), e.g. Dispel Magic on magic; property causes
    // (the aura found per member and type) bound into its causes shows what the dispel removes.
    class DispelProvider_GlobalTask : public STAI_DataProviderBase, public ISTAI_DebugSpells
    {
    public:
        DispelProvider_GlobalTask(uint32 dispelMask, STAI_SpellFilter filter) : dispelMask(dispelMask), filter(std::move(filter)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "DispelProvider_GlobalTask"; }
        inline static std::string task_name = "DispelProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<DispelProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override { return filter.observe ? filter.spells : std::vector<uint32>(); }

        // a harmful aura of a watched dispel type that the filter allows
        bool IsWatchedHolder(const SpellAuraHolder* holder) const;
        // the watched dispel types of the unit's harmful auras (1 << DISPEL_*); the first aura of each type is added
        // to causes
        uint32 GetWatchedDispels(const Unit* unit, STAI_SpellCauses& causes) const;

    protected:
        // <Dispel type="..."/> as a mask of 1 << DISPEL_*; fallback when none is configured
        static uint32 ParseDispelMask(const STAI_TaskConfig& config, uint32 fallback);
        // throws on children other than <Dispel>, <Observe>, <Ignore>, and on a bad <Observe> / <Ignore>
        static void ValidateChildren(const STAI_TaskConfig& config);

    private:
        uint32 dispelMask;          // 1 << DISPEL_* of the watched types
        STAI_SpellFilter filter;    // <Observe> / <Ignore>
    };

    // The DispelProvider for curses only, for the trees that only decurse (Mage):
    //   <Task class="CurseProvider_GlobalTask"/>               every dispellable curse
    //   <Task class="CurseProvider_GlobalTask">
    //     <Observe spellid="8399,12345"/>                        only these auras (any rank); spellid="": none
    //     <Ignore spellid="8399,12345"/>                         or, not with <Observe>: never these auras (any rank)
    //   </Task>
    // Properties and delegate as DispelProvider_GlobalTask (whoNeeds, curse, causes, onNeedsHelp).
    class CurseProvider_GlobalTask : public DispelProvider_GlobalTask
    {
    public:
        using DispelProvider_GlobalTask::DispelProvider_GlobalTask;

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "CurseProvider_GlobalTask"; }
        inline static std::string task_name = "CurseProvider";
        virtual std::string GetName() const override { return task_name; }
    };
}
