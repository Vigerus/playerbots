#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

class Player;
class Unit;

namespace ai
{
    // runtime context of MeleeProvider_GlobalTask: the bot against its melee target
    struct MeleeProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // property "target" (bound): the unit the bot fights, e.g. TargetProvider_GlobalTask::target
        ObjectGuid target;

        // fired when inMelee changed:
        //   <Transition type="OnDelegate" source="Root|MeleeProvider_GlobalTask::onMeleeChanged" node="Root"/>
        FSTAI_Delegate onMeleeChanged;
        // fired when canCharge turned true:
        //   <Transition type="OnDelegate" source="Root|MeleeProvider_GlobalTask::onCanCharge" node="Root|Charge"/>
        FSTAI_Delegate onCanCharge;

        // properties:
        //   inMelee: the target is alive and in melee reach of the bot
        //   behind: the bot stands outside the target's front arc (no parry, no block, no frontal cleave); a target
        //     that turned to the bot for less than a second (it turns with its victim) still counts as behind
        //   targetAttacksBot: the target's victim is the bot
        //   targetHealthPct: the target's health (100 without a target): Execute below 20
        //   targetDistance: yards between the bot and the target (0 without a target)
        //   enemiesInMelee: hostile units fighting the group within 8 yards of the bot (Cleave, Whirlwind, Thunder Clap)
        //   inCombat: the bot itself is in combat (Charge works only out of it)
        //   canCharge: a charge (Charge, Intercept) at the target pulls nothing else (see the class comment)
        //   attacking: the bot's auto attack is on the target
        //   swingQueued: an on-next-swing ability (Heroic Strike, Cleave) waits for the next swing
        bool inMelee = false;
        bool behind = false;
        bool targetAttacksBot = false;
        float targetHealthPct = 100.0f;
        float targetDistance = 0.0f;
        uint32 enemiesInMelee = 0;
        bool inCombat = false;
        bool canCharge = false;
        bool attacking = false;
        bool swingQueued = false;

        uint32 frontSinceMs = 0;    // since when the bot is in the target's front arc (0: it is not)
        ObjectGuid fightTarget;     // the target fightStartMs belongs to
        uint32 fightStartMs = 0;    // when the target was first seen fighting the group (0: not yet)
        uint32 sinceCheckMs = 0;
    };

    // Global task for melee fighters, about their target (bound, ticked after the provider that sets it):
    //   <Task class="MeleeProvider_GlobalTask">
    //     <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //     <ChargeRange min="8" max="25"/>   yards of Charge / Intercept (default 8 - 25)
    //     <PullRadius value="15"/>          no other hostile unit out of combat this close to the target or the bot
    //                                       (default 15)
    //     <FightTime value="2000"/>         ms the target must have been fighting the group (default 2000)
    //   </Task>
    // canCharge keeps charges from pulling more: the target fights the group (it is on a member's threat list, the
    // victim is not the bot) for FightTime already, it is within ChargeRange and in sight, no other hostile creature
    // that is not fighting yet stands within PullRadius of the target or of the bot, and the way does not enter a
    // dungeon stage that is not cleared. Ticked: checks every 100ms.
    class MeleeProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        struct Settings
        {
            float chargeMin = 8.0f;
            float chargeMax = 25.0f;
            float pullRadius = 15.0f;
            uint32 fightTimeMs = 2000;
        };

        explicit MeleeProvider_GlobalTask(const Settings& settings) : settings(settings) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "MeleeProvider_GlobalTask"; }
        inline static std::string task_name = "MeleeProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<MeleeProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        // the hostile units fighting the bot's group within range yards of the bot
        static uint32 CountEnemiesNear(PlayerbotAI* ai, Player* bot, float range);
        // a hostile creature that is not fighting yet within radius of the point (it would join a fight there)
        static bool IsIdleEnemyNear(Player* bot, float x, float y, float z, float radius, Unit* except);

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);
        void Update(MeleeProvider_GlobalTaskContext& context) const;

        Settings settings;
    };
}
