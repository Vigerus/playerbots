#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"

#include "Entities/ObjectGuid.h"
#include "MotionGenerators/MotionMaster.h"

#include <memory>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class WorldObject;

namespace ai
{
    // what a goal wants: walk to a location (any map, a path across dungeon portals), to a spot on the bot's map, or
    // into range of a unit (or a corpse)
    enum class MovementGoalKind : uint8 { Location, Position, Range };
    // where in range of the unit: anywhere (the side the bot is on), its back (unless it attacks the bot), its front
    enum class MovementSide : uint8 { Any, Behind, Front };

    // a goal of a MovementGoal_GlobalTask, submitted for one update while its conditions pass and it has its spot
    struct MovementGoalRequest
    {
        const void* owner = nullptr;    // the goal's context: the same goal in the next update
        std::string name;               // name="..." of the goal
        std::string source;             // "<goal name>" or "<goal name> (<reason>)"
        int32 priority = 0;             // the highest one is driven
        MovementGoalKind kind = MovementGoalKind::Position;
        STAI_Location location;         // Location
        STAI_Position position;         // Position
        ObjectGuid target;              // Range: a unit, or a corpse with dead
        float radius = 2.0f;            // Location / Position: arrived within; Range: the farthest range (melee: reach)
        float minRange = 0.0f;          // Range: not closer
        bool melee = false;             // Range: melee reach (CanReachWithMeleeAttack), the bot stays on the unit
        bool sight = true;              // Range: needs line of sight
        bool dead = false;              // Range: the target is a dead unit or a corpse
        MovementSide side = MovementSide::Any;
        bool interruptCast = false;     // moves also while the bot casts (a hazard)
    };

    // what the movement does: moving to the goal, reached it, blocked (no way, a stage that is not cleared), yielding to
    // movement it did not give, stopped (the goal is not wanted any more, it was not reached), none (dead, nothing yet)
    enum class MovementState : uint8 { None, Moving, Arrived, Blocked, Yielding, Stopped };

    // The bot's movement: the trees' MovementGoal_GlobalTask goals submit what they want every update (Submit), the
    // controller drives the one with the highest priority (Update, after the state machine) and is the only code
    // that gives the bot movement, always running (point moves with FORCED_MOVEMENT_RUN: without it a player walks;
    // paths run, chases do not walk). A goal stays the one shown after it is not wanted any more (reached, or stopped
    // before), with its source, until another goal drives; before the first goal it is Stay (the bot stands). It yields to movement it did not give (a charge, fear, the rtsc move) and gives none
    // while the bot casts (unless the goal interrupts casts). Location: a path from the PathfindingWorker (across
    // dungeon portals), walked leg by leg, planned again when the location moves away. Position: a point move on the
    // map. Range: melee chases the unit (at its back for Behind, after 1s in its front arc it walks around), ranged
    // chases into range and sight and stops there; a corpse is walked to. Not after a unit into a dungeon stage that
    // is not cleared, except its boxes that may be entered in combat while the group fights the unit, or when the unit
    // fights the group, attacks a member and no hostile creature that is not fighting yet is within 15 yards of it.
    class PlayerbotMovement
    {
    public:
        void Submit(const MovementGoalRequest& request) { requests.push_back(request); }
        void Update(PlayerbotAI* ai);

        // the goal driven in the last update, else the last one driven (reached / stopped), else Stay
        const MovementGoalRequest& GetActive() const { return hasActive || hasLast ? active : stay; }
        // a tree's goal drives the bot (not Stay)
        bool HasTreeGoal() const { return hasActive; }
        MovementState GetState() const { return state; }
        const char* GetStateName() const;
        // where the driven goal leads (the target's position for Range), unset for none
        const STAI_Location& GetDestination() const { return destination; }

        // the unit / corpse of a Range goal, null when gone (dead: dead units and corpses, else living units)
        static WorldObject* FindTarget(Player* bot, ObjectGuid guid, bool dead);
        // a Range goal is reached: in range (melee reach, or within radius) and not closer than minRange, in sight
        static bool IsInRange(Player* bot, const MovementGoalRequest& goal, WorldObject* target);

    private:
        void Drive(PlayerbotAI* ai, const MovementGoalRequest& goal, bool changed);
        void DriveLocation(PlayerbotAI* ai, const MovementGoalRequest& goal);
        void DrivePosition(PlayerbotAI* ai, const MovementGoalRequest& goal);
        void DriveRange(PlayerbotAI* ai, const MovementGoalRequest& goal);
        // stops the movement the controller gave (left alone when somebody else gave other movement since)
        void StopOwn(Player* bot);
        // remembers the movement just given
        void Gave(MovementGeneratorType type) { ownType = type; ownActive = true; }
        void ResetDrive();

        std::vector<MovementGoalRequest> requests;      // this update's goals
        MovementGoalRequest active;
        bool hasLast = false;                       // active is the last goal, not wanted any more
        // the goal without a wanted one: stand where the bot is
        MovementGoalRequest stay = MakeStay();
        static MovementGoalRequest MakeStay()
        {
            MovementGoalRequest goal;
            goal.name = "Stay";
            goal.source = "no goal yet";
            goal.priority = 0;
            return goal;
        }
        bool hasActive = false;
        MovementState state = MovementState::None;
        STAI_Location destination;

        bool ownActive = false;                     // the controller gave the bot movement (ownType)
        MovementGeneratorType ownType = IDLE_MOTION_TYPE;

        // Location: the path and how far it is walked
        std::shared_ptr<const NavPathRequest> pathRequest;
        uint32 pathMap = 0;                         // the bot's map when it was requested
        uint32 requestMs = 0;
        uint32 nextPlanMs = 0;                      // no new request before (a failed one waits)
        bool walking = false;                       // the leg is being walked (MovePath)
        bool pushed = false;                        // walking into the leg's portal (MovePoint)
        uint32 pushedMs = 0;
        // Position: the spot of the point move
        STAI_Position moveSpot;
        // Range: the unit of the chase, its angle, the walk around to its back
        ObjectGuid chased;
        float chasedAngle = 0.0f;
        float chasedOffset = 0.0f;
        bool goingBehind = false;
        uint32 frontSinceMs = 0;
    };
}
