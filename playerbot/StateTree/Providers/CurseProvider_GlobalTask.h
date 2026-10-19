#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/Providers/STAI_GroupAuraWatch.h"

#include <string>
#include <vector>

namespace ai
{
    class CurseProvider_GlobalTask;

    // runtime context of CurseProvider_GlobalTask: the bot's subscriptions and who is cursed right now
    struct CurseProvider_GlobalTaskContext : public STAI_GroupAuraWatchContext
    {
        // fired whenever an evaluation finds a cursed member:
        //   <Transition type="OnDelegate" source="Root|CurseProvider_GlobalTask::onNeedsHelp" node="..."/>
        FSTAI_Delegate onNeedsHelp;
        // the cursed members (self included), the bot first; refreshed on every aura / group event:
        //   <Binding local="whoNeeds" external="Root|CurseProvider_GlobalTask::whoNeeds"/>
        std::vector<ObjectGuid> whoNeeds;

        const CurseProvider_GlobalTask* task = nullptr;

        virtual void Evaluate(Group* leftGroup = nullptr) override;
        virtual bool IsWatchedAura(const SpellAuraHolder* holder) const override;
    };

    // Global task that watches the bot and its group for curses: property whoNeeds (the cursed members), delegate
    // onNeedsHelp (fired when an evaluation finds any). Not tickable, like BuffProvider_GlobalTask.
    //   <Global>
    //     <Task class="CurseProvider_GlobalTask"/>               every dispellable curse
    //     <Task class="CurseProvider_GlobalTask">
    //       <Aura spellid="..."/>                                  only these auras (any rank)
    //     </Task>
    //   </Global>
    // Task_CastSpell casts on them (whoNeeds bound into it), e.g. Remove Lesser Curse.
    class CurseProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        explicit CurseProvider_GlobalTask(std::vector<uint32> auras) : auras(std::move(auras)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "CurseProvider_GlobalTask"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<CurseProvider_GlobalTaskContext>(); }
        virtual void InitContext(STAI_TaskContext& context) const override;

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;

        // a dispellable curse (no <Aura> configured), or a rank of one of the configured auras
        bool IsWatchedSpell(const SpellEntry* spellInfo) const;
        bool HasWatchedAura(const Unit* unit) const;

    private:
        static std::vector<uint32> ParseAuras(const STAI_TaskConfig& config);

        std::vector<uint32> auras;  // first ranks; empty: every dispellable curse
    };
}
