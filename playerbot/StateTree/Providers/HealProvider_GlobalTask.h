#pragma once

#include "playerbot/StateTree/Providers/STAI_TeamRoles.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <vector>

class Player;

namespace ai
{
    // runtime context of HealProvider_GlobalTask: who needs healing, who has aggro and who is dead
    struct HealProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // the living members (the bot included) below each threshold, the lowest health first (a tank before others
        // of the same health); a lower band's members are in every band above it:
        //   <Binding local="whoNeeds" external="Root|HealProvider_GlobalTask::low"/>
        std::vector<ObjectGuid> critical;
        std::vector<ObjectGuid> low;
        std::vector<ObjectGuid> medium;
        std::vector<ObjectGuid> almostFull;
        // the first of almostFull (the member to get in range of), and its health; 100 without one
        ObjectGuid healTarget;
        float lowestPct = 100.0f;
        // fired when a member got into a band (almostFull first, or a lower one than before):
        //   <Transition type="OnDelegate" source="Root|HealProvider_GlobalTask::onNeedsHeal" node="Root"/>
        FSTAI_Delegate onNeedsHeal;

        // the living members that are no tank (the bot included) and that a hostile unit attacks (they took aggro),
        // the bot first; fired when one is new:
        std::vector<ObjectGuid> aggroed;
        FSTAI_Delegate onAggro;
        // a hostile unit attacks the bot (also when the bot tanks)
        bool selfAttacked = false;

        // the dead members that a resurrection reaches, no resurrection offered yet: on the bot's map with the body
        // not released (the player's guid), or released (a ghost anywhere, e.g. a bot logged out dead and back in)
        // with the corpse on the bot's map (the corpse's guid: Task_GetInRange / Task_CastSpell with
        // <Target value="dead"/> go to it and cast at it); fired when one is new:
        std::vector<ObjectGuid> dead;
        FSTAI_Delegate onDead;
        // the first of dead (to walk to, Task_GetInRange with <Target value="dead"/>)
        ObjectGuid resurrectTarget;

        // the first living tank other than the bot (TeamProvider_GlobalTask::FindMainTank), and whether the bot is
        // beyond TankRange of it or does not see it; fired when tankOutOfRange turned true:
        ObjectGuid mainTank;
        bool tankOutOfRange = false;
        FSTAI_Delegate onTankOutOfRange;

        STAI_TeamRoles roles;
        uint32 sinceCheckMs = 0;
    };

    // Global task that watches the health of the bot and its group (the members on its map), for heal nodes:
    //   <Global>
    //     <Task class="HealProvider_GlobalTask">
    //       <Critical value="20"/>      percent, the bands' upper bounds (defaults 20 / 50 / 70 / 90)
    //       <Low value="50"/>
    //       <Medium value="70"/>
    //       <AlmostFull value="90"/>
    //       <TankRange value="30"/>     yards to the main tank before tankOutOfRange (default 30)
    //     </Task>
    //   </Global>
    // Properties critical / low / medium / almostFull (member lists), healTarget, lowestPct, aggroed, selfAttacked,
    // dead, resurrectTarget, mainTank, tankOutOfRange; delegates onNeedsHeal, onAggro, onDead, onTankOutOfRange. Roles (who tanks) come
    // from STAI_TeamRoles. Ticked: checks every 200ms (nothing broadcasts health changes).
    class HealProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Settings
        {
            float critical = 20.0f;
            float low = 50.0f;
            float medium = 70.0f;
            float almostFull = 90.0f;
            float tankRange = 30.0f;
        };

        explicit HealProvider_GlobalTask(Settings settings) : settings(settings) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "HealProvider_GlobalTask"; }
        inline static std::string task_name = "HealProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<HealProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);
        void Evaluate(HealProvider_GlobalTaskContext& context) const;

        Settings settings;
    };
}
