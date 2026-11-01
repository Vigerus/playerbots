#pragma once

#include "playerbot/StateTree/Providers/Combat/STAI_TargetIcons.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Player;
class Unit;

namespace ai
{
    template<class T> class Value;

    class CCProvider_GlobalTask;

    // runtime context of CCProvider_GlobalTask: the target to crowd-control right now
    struct CCProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the target to crowd-control changed to a (new) one:
        //   <Transition type="OnDelegate" source="Root|CCProvider_GlobalTask::onCCNeeded" node="..."/>
        FSTAI_Delegate onCCNeeded;
        // the target to crowd-control (none or one), for Task_CastSpell:
        //   <Binding local="whoNeeds" external="Root|CCProvider_GlobalTask::whoNeeds"/>
        std::vector<ObjectGuid> whoNeeds;

        uint32 sinceCheckMs = 0;
        uint32 sinceFireMs = 0;
        uint32 iconsVersion = 0;    // PlayerbotTargetIcons::GetVersion of the last check: a change checks again now
    };

    // Global task that watches the bot's crowd-control marks (its "rti cc" icons, default moon) in the bot's group:
    // the first marked unit (in the order of the bot's rti cc list) that is alive, hostile, in sight, not under crowd
    // control yet (polymorph, frost, sap, gouge, shackle, ... or one of the <Aura> spells) and not the target the
    // group kills first (the first living target marked with one of the bot's "rti" icons, default skull) needs crowd
    // control. With <Unmarked> and no such mark, one unmarked unit fighting the bot is crowd-controlled while two or
    // more fight it: never the bot's own target, only a creature the spell can affect (its creature types), the one with
    // the most health; none while a unit already has that spell's aura (one at a time):
    //   <Global>
    //     <Task class="CCProvider_GlobalTask">
    //       <Aura spellid="..."/>      optional: these auras also count as crowd-controlled (any rank)
    //       <Unmarked spellid="118"/>  optional: the spell for an unmarked unit (any rank), e.g. Polymorph
    //     </Task>
    //   </Global>
    // Ticked (nothing broadcasts mark changes or deaths): checks every 250ms; onCCNeeded fires when the target
    // changes to another unit (a new mark, the old crowd control broke) and again every second while it still needs
    // crowd control (a cast that could not be done, out of range or sight, is retried).
    class CCProvider_GlobalTask : public STAI_DataProviderBase, public ISTAI_DebugSpells
    {
    public:
        struct Settings
        {
            std::vector<uint32> auras;      // first ranks
            uint32 unmarkedSpell = 0;       // first rank, 0 without <Unmarked>
        };

        explicit CCProvider_GlobalTask(Settings settings) : auras(std::move(settings.auras)), unmarkedSpell(settings.unmarkedSpell) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "CCProvider_GlobalTask"; }
        inline static std::string task_name = "CCProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<CCProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override { return auras; }

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);
        // the living units marked with the icons (rti / rti cc), in the icons' order
        static std::vector<Unit*> GetMarkedUnits(const STAI_TaskContext& context, const std::vector<std::string>& icons);
        bool IsCrowdControlled(Unit* unit, Player* bot) const;
        ObjectGuid FindTarget(const CCProvider_GlobalTaskContext& context) const;
        ObjectGuid FindUnmarkedTarget(const CCProvider_GlobalTaskContext& context, Unit* killFirst) const;
        void Update(CCProvider_GlobalTaskContext& context) const;

        std::vector<uint32> auras;  // first ranks
        uint32 unmarkedSpell;
    };
}
