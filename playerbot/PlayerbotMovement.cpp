#include "PlayerbotMovement.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/MeleeProvider_GlobalTask.h"

#include "Entities/Corpse.h"
#include "Entities/Player.h"
#include "Maps/Map.h"
#include "Server/DBCStores.h"
#include "World/World.h"

#include <algorithm>

using namespace ai;

namespace
{
    // a path request may take this long before the goal is blocked
    constexpr uint32 pathTimeoutMs = 10000;
    // a failed path is requested again after this long
    constexpr uint32 retryMs = 3000;
    // no new path request sooner than this after the last one
    constexpr uint32 replanMs = 1000;
    // walking into a portal's area trigger this long without a teleport: plan again
    constexpr uint32 portalTimeoutMs = 10000;
    // in the unit's front arc this long before the bot walks to its back (units turn with their victim)
    constexpr uint32 frontGraceMs = 1000;
    // how far behind the unit's back the bot stands (inside melee reach)
    constexpr float behindDistance = 1.0f;
    // into a stage that is not cleared after an engaged unit: no hostile creature that is not fighting yet this close to it
    constexpr float stageIdleRadius = 15.0f;
    // ranged: the chase ends at this share of the range, safely inside it
    constexpr float rangeFactor = 0.85f;
    // in range without sight: the bot closes in to this distance
    constexpr float closeDistance = 3.0f;

    // the time at has come (getMSTime wraps around)
    bool Reached(uint32 at, uint32 now)
    {
        return WorldTimer::getMSTimeDiff(at, now) < 0x80000000;
    }

    // the leg of the path on the map, nullptr when the path does not cross it
    const NavPathSegment* FindLeg(const NavPathRequest& path, uint32 mapId, bool& last)
    {
        for (size_t i = 0; i < path.segments.size(); ++i)
            if (path.segments[i].mapId == mapId)
            {
                last = i + 1 == path.segments.size();
                return &path.segments[i];
            }
        return nullptr;
    }

    // where the leg ends: the destination, or the centre of its navlink's area trigger
    Vector3 GetLegEnd(const NavPathRequest& path, const NavPathSegment& leg, bool last)
    {
        if (last)
            return path.dest;
        if (AreaTriggerEntry const* entry = sAreaTriggerStore.LookupEntry(leg.areaTriggerId))
            return Vector3(entry->x, entry->y, entry->z);
        return leg.points.back();
    }
}

const char* PlayerbotMovement::GetStateName() const
{
    switch (state)
    {
        case MovementState::Moving:   return "moving";
        case MovementState::Arrived:  return "reached";
        case MovementState::Stopped:  return "stopped";
        case MovementState::Blocked:  return "blocked";
        case MovementState::Yielding: return "yielding";
        default:                      return "none";
    }
}

WorldObject* PlayerbotMovement::FindTarget(Player* bot, ObjectGuid guid, bool dead)
{
    if (guid.IsEmpty() || !bot->IsInWorld())
        return nullptr;
    if (guid.IsCorpse())
    {
        Corpse* corpse = dead ? bot->GetMap()->GetCorpse(guid) : nullptr;
        return corpse && corpse->IsInWorld() ? corpse : nullptr;
    }
    Unit* unit = guid == bot->GetObjectGuid() ? bot : bot->GetMap()->GetUnit(guid);
    if (!unit || !unit->IsInWorld() || sServerFacade.IsAlive(unit) == dead)
        return nullptr;
    return unit;
}

bool PlayerbotMovement::IsInRange(Player* bot, const MovementGoalRequest& goal, WorldObject* target)
{
    if (!target || target == bot)
        return target == bot;
    const float distance = bot->GetDistance(target);
    if (distance < goal.minRange)
        return false;
    const bool reach = goal.melee && target->isType(TYPEMASK_UNIT) ? bot->CanReachWithMeleeAttack(static_cast<Unit*>(target)) : distance <= goal.radius;
    return reach && (!goal.sight || bot->IsWithinLOSInMap(target));
}

void PlayerbotMovement::ResetDrive()
{
    pathRequest.reset();
    walking = false;
    pushed = false;
    nextPlanMs = 0;
    moveSpot = STAI_Position();
    chased.Clear();
    goingBehind = false;
    frontSinceMs = 0;
}

void PlayerbotMovement::StopOwn(Player* bot)
{
    if (!ownActive)
        return;
    ownActive = false;
    MotionMaster& motion = *bot->GetMotionMaster();
    if (motion.GetCurrentMovementGeneratorType() == ownType)
    {
        motion.Clear();
        bot->StopMoving();
    }
}

void PlayerbotMovement::Update(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();

    // the goal with the highest priority, the first submitted of equal ones
    const MovementGoalRequest* best = nullptr;
    for (const MovementGoalRequest& request : requests)
        if (!best || request.priority > best->priority)
            best = &request;

    // a living bot or a ghost (the Dead tree's corpse run) moves, a body does not; a ghost is not IsAlive (its death
    // state is DEAD)
    const bool canMove = bot->IsInWorld() && (sServerFacade.IsAlive(bot) || bot->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST));

    // no wanted goal: the bot stands where it is (the movement the controller gave stops); the last goal stays shown,
    // reached or stopped before it was
    if (!best || !canMove)
    {
        StopOwn(bot);
        if (hasActive)
        {
            hasLast = true;
            if (state != MovementState::Arrived)
                state = MovementState::Stopped;
        }
        hasActive = false;
        if (!canMove)
            state = MovementState::None;
        else if (!hasLast)
            state = MovementState::Arrived;
        destination = STAI_Location();
        ResetDrive();
        requests.clear();
        return;
    }

    const bool changed = !hasActive || active.owner != best->owner;
    active = *best;
    hasActive = true;
    hasLast = false;
    requests.clear();
    if (changed)
        ResetDrive();
    Drive(ai, active, changed);
}

void PlayerbotMovement::Drive(PlayerbotAI* ai, const MovementGoalRequest& goal, bool changed)
{
    Player* bot = ai->GetBot();
    MotionMaster& motion = *bot->GetMotionMaster();
    const MovementGeneratorType current = motion.GetCurrentMovementGeneratorType();

    // movement somebody else gave (a charge, fear, a spell, the rtsc move): left alone until it is over
    const bool own = ownActive && current == ownType;
    if (current != IDLE_MOTION_TYPE && current != FOLLOW_MOTION_TYPE && !own && !(current == CHASE_MOTION_TYPE && goal.kind == MovementGoalKind::Range))
    {
        state = MovementState::Yielding;
        ownActive = false;
        ResetDrive();
        return;
    }
    // no new movement while casting (it would interrupt the cast), unless the goal must (a hazard)
    if (bot->IsNonMeleeSpellCasted(false) && !goal.interruptCast)
        return;
    if (bot->IsBeingTeleported() || !ai->CanMove())
    {
        state = MovementState::Moving;
        return;
    }
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    switch (goal.kind)
    {
        case MovementGoalKind::Location: DriveLocation(ai, goal); break;
        case MovementGoalKind::Position: DrivePosition(ai, goal); break;
        case MovementGoalKind::Range:    DriveRange(ai, goal); break;
    }
}

void PlayerbotMovement::DriveLocation(PlayerbotAI* ai, const MovementGoalRequest& goal)
{
    Player* bot = ai->GetBot();
    MotionMaster& motion = *bot->GetMotionMaster();
    const STAI_Location& location = goal.location;
    destination = location;
    const uint32 now = WorldTimer::getMSTime();

    // arrived
    if (location.mapId == bot->GetMapId() && bot->GetDistance(location.x, location.y, location.z) <= goal.radius)
    {
        StopOwn(bot);
        ResetDrive();
        state = MovementState::Arrived;
        return;
    }

    // a path: none yet, the bot changed map (a portal), or the location moved away from where the path leads
    const bool stale = !pathRequest || pathMap != bot->GetMapId() ||
        (pathRequest->IsReady() && (location.mapId != pathRequest->destMapId ||
            (Vector3(location.x, location.y, location.z) - pathRequest->dest).length() > goal.radius));
    if (stale && Reached(nextPlanMs, now))
    {
        pathRequest = sPathfindingWorker.Request(bot->GetMapId(), Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()),
            location.mapId, Vector3(location.x, location.y, location.z));
        pathMap = bot->GetMapId();
        requestMs = now;
        nextPlanMs = now + replanMs;
        pushed = false;
        // the new path's leg is walked once it is ready; the bot walks on along the old one meanwhile
        walking = false;
    }
    if (!pathRequest)
    {
        state = MovementState::Blocked;
        return;
    }
    if (!pathRequest->IsDone())
    {
        if (WorldTimer::getMSTimeDiff(requestMs, now) > pathTimeoutMs)
        {
            pathRequest.reset();
            nextPlanMs = now + retryMs;
            state = MovementState::Blocked;
            return;
        }
        state = MovementState::Moving;
        return;
    }
    if (!pathRequest->IsReady() || pathRequest->segments.empty())
    {
        // no way there (another continent without a portal, no navmesh way): stand, try again later
        StopOwn(bot);
        pathRequest.reset();
        walking = false;
        nextPlanMs = now + retryMs;
        state = MovementState::Blocked;
        return;
    }

    bool last = false;
    const NavPathSegment* leg = FindLeg(*pathRequest, bot->GetMapId(), last);
    if (!leg)
    {
        pathRequest.reset();
        walking = false;
        state = MovementState::Blocked;
        return;
    }
    const Vector3 end = GetLegEnd(*pathRequest, *leg, last);
    state = MovementState::Moving;

    // a new path (or none walked yet): walk its leg on this map
    const MovementGeneratorType current = motion.GetCurrentMovementGeneratorType();
    const bool moving = ownActive && current == ownType && (current == PATH_MOTION_TYPE || current == POINT_MOTION_TYPE);
    if (!walking)
    {
        // the worker has no unit to put the points on the ground with; the first point is where the bot stood
        std::vector<G3D::Vector3> points(leg->points.begin() + 1, leg->points.end());
        if (sWorld.getConfig(CONFIG_BOOL_PATH_FIND_NORMALIZE_Z))
            for (G3D::Vector3& point : points)
                bot->UpdateAllowedPositionZ(point.x, point.y, point.z);
        if (points.empty())
            points.push_back(end);
        motion.MovePath(points, FORCED_MOVEMENT_RUN, false, false);
        Gave(PATH_MOTION_TYPE);
        walking = true;
        return;
    }
    if (moving)
        return;

    // the leg ended short of a portal: straight into its area trigger, then wait for the teleport
    if (!last)
    {
        if (!pushed)
        {
            motion.MovePoint(0, end.x, end.y, end.z, FORCED_MOVEMENT_RUN, false);
            Gave(POINT_MOTION_TYPE);
            pushed = true;
            pushedMs = now;
        }
        else if (WorldTimer::getMSTimeDiff(pushedMs, now) > portalTimeoutMs)
        {
            pathRequest.reset();
            walking = false;
            state = MovementState::Blocked;
        }
        return;
    }

    // the path ended short (length limit, incomplete navmesh path): plan again from here
    pathRequest.reset();
    walking = false;
}

void PlayerbotMovement::DrivePosition(PlayerbotAI* ai, const MovementGoalRequest& goal)
{
    Player* bot = ai->GetBot();
    MotionMaster& motion = *bot->GetMotionMaster();
    const STAI_Position& spot = goal.position;
    destination = { bot->GetMapId(), spot.x, spot.y, spot.z, true };

    if (bot->GetDistance(spot.x, spot.y, spot.z) <= goal.radius)
    {
        StopOwn(bot);
        moveSpot = STAI_Position();
        state = MovementState::Arrived;
        return;
    }

    state = MovementState::Moving;
    const uint32 now = WorldTimer::getMSTime();
    const bool moving = ownActive && motion.GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE;
    // a new spot, or the move ended outside the radius (tried again after a moment)
    if (moveSpot != spot || (!moving && Reached(nextPlanMs, now)))
    {
        // the path comes from the PathfindingWorker
        motion.MovePointAsync(0, spot.x, spot.y, spot.z, FORCED_MOVEMENT_RUN);
        Gave(POINT_MOTION_TYPE);
        moveSpot = spot;
        nextPlanMs = now + replanMs;
    }
}

void PlayerbotMovement::DriveRange(PlayerbotAI* ai, const MovementGoalRequest& goal)
{
    Player* bot = ai->GetBot();
    MotionMaster& motion = *bot->GetMotionMaster();
    WorldObject* target = FindTarget(bot, goal.target, goal.dead);
    if (!target || target == bot)
    {
        StopOwn(bot);
        destination = STAI_Location();
        state = target ? MovementState::Arrived : MovementState::Blocked;
        return;
    }
    destination = { target->GetMapId(), target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), true };
    const bool inRange = IsInRange(bot, goal, target);
    const uint32 now = WorldTimer::getMSTime();
    Unit* unit = target->isType(TYPEMASK_UNIT) ? static_cast<Unit*>(target) : nullptr;

    // not after a unit in a dungeon stage that is not cleared, except into the boxes that may be entered in combat while
    // the group fights it, or after a unit the group fights that attacks a member (engaged where it stands) with no
    // hostile creature that is not fighting yet within 15 yards of it (no second pack pulled)
    if (!inRange)
    {
        const auto& attackers = ai->GetTeamAttackers();
        const bool fightsGroup = attackers.find(target->GetObjectGuid()) != attackers.end();
        const bool fighting = bot->IsInCombat() || fightsGroup;
        Unit* victim = unit ? unit->GetVictim() : nullptr;
        const bool engaged = fightsGroup && victim && (victim == bot || (victim->IsPlayer() && bot->IsInGroup(static_cast<Player*>(victim), true)));
        if (ai->IsStageEntryBlocked(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), fighting) &&
            (!engaged || MeleeProvider_GlobalTask::IsIdleEnemyNear(bot, target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), stageIdleRadius, unit)))
        {
            StopOwn(bot);
            chased.Clear();
            state = MovementState::Blocked;
            return;
        }
    }

    // walking around the unit: wait until the point behind it is reached
    if (goingBehind)
    {
        if (ownActive && motion.GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        {
            state = MovementState::Moving;
            return;
        }
        goingBehind = false;
        chased.Clear();
    }

    if (goal.melee && unit)
    {
        // behind: not while the unit attacks the bot (it turns to the bot anyway)
        const bool wantBehind = goal.side == MovementSide::Behind && unit->GetVictim() != bot;
        if (wantBehind && inRange && unit->HasInArc(bot))
        {
            if (!frontSinceMs)
                frontSinceMs = std::max<uint32>(1, now);
            if (WorldTimer::getMSTimeDiff(frontSinceMs, now) >= frontGraceMs)
            {
                float x, y, z;
                unit->GetNearPoint(bot, x, y, z, bot->GetObjectBoundingRadius(), behindDistance, unit->GetOrientation() + M_PI_F);
                motion.MovePoint(0, x, y, z, FORCED_MOVEMENT_RUN);
                Gave(POINT_MOTION_TYPE);
                goingBehind = true;
                frontSinceMs = 0;
                state = MovementState::Moving;
                return;
            }
        }
        else
            frontSinceMs = 0;

        // the chase keeps the bot on the unit (ChaseMovementGenerator: 0 keeps the side the bot is on)
        const float angle = wantBehind ? M_PI_F : 0.0f;
        if (!(ownActive && motion.GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE) || chased != unit->GetObjectGuid() || chasedAngle != angle)
        {
            motion.MoveChase(unit, 0.0f, angle);
            Gave(CHASE_MOTION_TYPE);
            chased = unit->GetObjectGuid();
            chasedAngle = angle;
            chasedOffset = 0.0f;
        }
        if (inRange && bot->IsStopped() && !bot->HasInArc(unit))
            sServerFacade.SetFacingTo(bot, unit);
        state = inRange ? MovementState::Arrived : MovementState::Moving;
        return;
    }

    // ranged, or a corpse: in range and sight it stands (casts do not move)
    if (inRange)
    {
        StopOwn(bot);
        chased.Clear();
        state = MovementState::Arrived;
        return;
    }

    state = MovementState::Moving;
    if (!unit)
    {
        // a corpse does not move: walk to it
        const STAI_Position spot = { target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), true };
        if (moveSpot != spot || !(ownActive && motion.GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE))
        {
            motion.MovePoint(0, spot.x, spot.y, spot.z, FORCED_MOVEMENT_RUN);
            Gave(POINT_MOTION_TYPE);
            moveSpot = spot;
        }
        return;
    }

    // chase into range; in range without sight the bot closes in until it sees the unit
    const float offset = bot->GetDistance(unit) <= goal.radius ? closeDistance : std::max(goal.minRange, goal.radius * rangeFactor);
    if (!(ownActive && motion.GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE) || chased != unit->GetObjectGuid() || chasedOffset != offset)
    {
        motion.MoveChase(unit, offset, 0.0f);
        Gave(CHASE_MOTION_TYPE);
        chased = unit->GetObjectGuid();
        chasedAngle = 0.0f;
        chasedOffset = offset;
    }
}
