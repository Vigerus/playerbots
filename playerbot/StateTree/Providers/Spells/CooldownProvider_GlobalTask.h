#pragma once

#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <map>
#include <vector>

namespace ai
{
    // runtime context of CooldownProvider_GlobalTask: which of the watched spells the bot can use now
    struct CooldownProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when a watched spell became ready or went on cooldown:
        //   <Transition type="OnDelegate" source="Root|CooldownProvider_GlobalTask::onCooldownChanged" node="..."/>
        FSTAI_Delegate onCooldownChanged;
        // per configured spell id: the bot knows the spell (any rank) and it is off cooldown, read by key:
        //   <Condition binding="Root|CooldownProvider_GlobalTask::ready" key="11958"/>
        std::map<uint32, bool> ready;

        uint32 sinceCheckMs = 0;
    };

    // Global task that watches the cooldowns of a list of the bot's spells:
    //   <Global>
    //     <Task class="CooldownProvider_GlobalTask">
    //       <Spell id="11958"/>    Ice Block: ready[11958]
    //       <Spell id="12051"/>    Evocation: ready[12051]
    //     </Task>
    //   </Global>
    // ready[id] is true while the bot knows the spell or another rank of it and that rank's own and category
    // cooldowns are over (the global cooldown does not count). Ticked: checks every 250ms; onCooldownChanged fires
    // when an entry changed (and on the first check).
    class CooldownProvider_GlobalTask : public STAI_DataProviderBase, public ISTAI_DebugSpells
    {
    public:
        // a configured spell and every rank of it, highest first
        struct WatchedSpell
        {
            uint32 spellId;
            std::vector<uint32> ranks;
        };

        explicit CooldownProvider_GlobalTask(std::vector<WatchedSpell> spells) : spells(std::move(spells)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "CooldownProvider_GlobalTask"; }
        inline static std::string task_name = "CooldownProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<CooldownProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

    private:
        static std::vector<WatchedSpell> ParseSpells(const STAI_TaskConfig& config);
        void Update(CooldownProvider_GlobalTaskContext& context, bool first) const;

        std::vector<WatchedSpell> spells;
    };
}
