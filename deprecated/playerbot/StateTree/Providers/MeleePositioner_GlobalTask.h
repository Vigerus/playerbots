#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

namespace ai
{
    // runtime context of MeleePositioner_GlobalTask: the movement the positioner gave the bot
    struct MeleePositioner_GlobalTaskContext : public STAI_TaskContext
    {
        // properties:
        //   victim: the unit the bot's auto attack is on (the one followed), empty when none
        //   inReach: the bot is in melee reach of it
        //   blocked: the victim stands in a dungeon stage the bot may not enter, the bot waits where it is
        ObjectGuid victim;
        bool inReach = false;
        bool blocked = false;

        ObjectGuid chased;          // the unit of the positioner's chase, empty when it gave none
        bool chasedBehind = false;  // that chase is at the unit's back
        bool goingBehind = false;   // the positioner's walk around the unit to its back (MovePoint)
        uint32 frontSinceMs = 0;    // since when the bot stands in the victim's front arc (0: it does not)
        uint32 sinceCheckMs = 0;
    };

    // Global task of the Reaction trees of melee classes: the bot's movement in melee, next to the main state's tree,
    // which only starts the auto attack (Task_StartAttack) and uses the abilities in reach:
    //   <Global>
    //     <Task class="MeleePositioner_GlobalTask">
    //       <Position value="auto"/>   behind (damage dealers: no parry, block or frontal cleave), front (tanks: the
    //                                  unit stays where it is) or auto (front for tanks, STAI roles; default)
    //     </Task>
    //   </Global>
    // Follows the unit the bot's auto attack is on (bot->GetVictim(), alive and hostile): chases it into melee reach
    // (MoveChase, at its back for behind), walks around it to its back when the bot stood in its front arc for 1s and it
    // attacks somebody else, faces it while standing. Leaves movement it did not give alone (escaping a hazard, a
    // charge, a point move of the tree, fear), takes over from none, a follow or another chase. Not into a dungeon stage
    // that is not cleared, except its boxes that may be entered in combat while the group fights the unit; no new
    // movement while the bot casts. Does nothing without a victim (the tree stopped the attack: threat). Ticked: every
    // 100ms.
    class MeleePositioner_GlobalTask : public STAI_GlobalTask
    {
    public:
        enum class Position { Auto, Behind, Front };

        explicit MeleePositioner_GlobalTask(Position position) : position(position) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "MeleePositioner_GlobalTask"; }
        inline static std::string task_name = "MeleePositioner";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<MeleePositioner_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

    private:
        static Position ParsePosition(const STAI_TaskConfig& config);
        void Update(MeleePositioner_GlobalTaskContext& context) const;

        Position position;
    };
}
