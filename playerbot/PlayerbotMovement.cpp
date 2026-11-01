#include "PlayerbotMovement.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/Combat/MeleeProvider_GlobalTask.h"

#include "Entities/Corpse.h"
#include "Entities/GameObject.h"
#include "Entities/Player.h"
#include "Entities/Transports.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Maps/Map.h"
#include "Server/WorldSession.h"
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
    // a taxi flight asked for that has not started after this long: plan again without flights for a while
    constexpr uint32 flightTimeoutMs = 5000;
    constexpr uint32 noFlightsMs = 60000;
    // the flight master is searched this close to the end of the leg
    constexpr float flightMasterRange = 10.0f;
    // waiting for a transport this long: plan again
    constexpr uint32 transportWaitMs = 15 * MINUTE * IN_MILLISECONDS;
    // a transport stands this close to where it is boarded / left
    constexpr float dockedDistance = 5.0f;
    // elevators are searched this far around the bot
    constexpr float elevatorRange = 100.0f;
    // on an elevator's platform this close to its centre
    constexpr float platformDistance = 2.0f;
    // a walked piece ended this far from its end: the bot was pushed off the path, plan again
    constexpr float offPathDistance = 8.0f;
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

    float Distance(const WorldObject* object, const Vector3& point)
    {
        return (Vector3(object->GetPositionX(), object->GetPositionY(), object->GetPositionZ()) - point).length();
    }

    // the flight master the leg ends at
    Creature* FindFlightMaster(Player* bot)
    {
        CreatureList found;
        MaNGOS::AnyUnitInObjectRangeCheck check(bot, flightMasterRange);
        MaNGOS::CreatureListSearcher<MaNGOS::AnyUnitInObjectRangeCheck> searcher(found, check);
        Cell::VisitGridObjects(bot, searcher, flightMasterRange);
        for (Creature* creature : found)
            if (Creature* master = bot->GetNPCIfCanInteractWith(creature->GetObjectGuid(), UNIT_NPC_FLAG_FLIGHTMASTER))
                return master;
        return nullptr;
    }

    // the transport of the leg: a boat or zeppelin of the map, else an elevator nearby
    GenericTransport* FindTransport(Player* bot, uint32 entry)
    {
        for (Transport* transport : bot->GetMap()->GetTransports())
            if (transport->GetEntry() == entry)
                return transport;

        GameObjectList found;
        MaNGOS::AllGameObjectsMatchingOneEntryInRange check(bot, { entry }, elevatorRange);
        MaNGOS::GameObjectListSearcher<MaNGOS::AllGameObjectsMatchingOneEntryInRange> searcher(found, check);
        Cell::VisitGridObjects(bot, searcher, elevatorRange);
        for (GameObject* object : found)
            if (GenericTransport* transport = dynamic_cast<GenericTransport*>(object))
                return transport;
        return nullptr;
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
    if (guid.IsGameObject())
    {
        GameObject* go = bot->GetMap()->GetGameObject(guid);
        return go && go->IsInWorld() && go->IsSpawned() ? go : nullptr;
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
    StartLeg(0);
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

    // a taxi flight: the path goes on where it lands
    if (bot->IsTaxiFlying())
    {
        tookOff = tookOff || step == LegStep::Flight;
        state = MovementState::Moving;
        return;
    }
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
    switch (goal.kind)
    {
        case MovementGoalKind::Location: DriveLocation(ai, goal); break;
        case MovementGoalKind::Position: DrivePosition(ai, goal); break;
        case MovementGoalKind::Range:    DriveRange(ai, goal); break;
    }

    // only a bot that has to move stands up: a reached goal leaves an eating / drinking bot seated
    if (state == MovementState::Moving && bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
}

void PlayerbotMovement::DriveLocation(PlayerbotAI* ai, const MovementGoalRequest& goal)
{
    Player* bot = ai->GetBot();
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

    // a path: none yet, or the location moved away from where the path leads; not while the bot is on a flight or a
    // transport
    const bool travelling = pathRequest && pathRequest->IsReady() && (step == LegStep::Flight || step == LegStep::Ride);
    const bool stale = !pathRequest ||
        (pathRequest->IsReady() && (location.mapId != pathRequest->destMapId ||
            (Vector3(location.x, location.y, location.z) - pathRequest->dest).length() > goal.radius));
    if (stale && !travelling && Reached(nextPlanMs, now))
    {
        pathRequest = sWorld.GetSubsystem<PathSubsystem>()->Request(bot->GetMapId(), Vector3(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ()),
            location.mapId, Vector3(location.x, location.y, location.z), true, NavTraveler::Of(bot, Reached(noFlightsUntilMs, now)));
        requestMs = now;
        nextPlanMs = now + replanMs;
        // the new path is walked from its start once it is ready; the bot walks on along the old one meanwhile
        StartLeg(0);
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
        // no way there (no route over the travel graph, no navmesh way): stand, try again later
        StopOwn(bot);
        Replan();
        nextPlanMs = now + retryMs;
        state = MovementState::Blocked;
        return;
    }

    // a portal or a transport took the bot to another map: its leg there (on a transport the leg is the ride)
    const std::vector<NavPathSegment>& legs = pathRequest->segments;
    if (step != LegStep::Ride && legs[legIndex].mapId != bot->GetMapId())
    {
        size_t next = legIndex + 1;
        while (next < legs.size() && legs[next].mapId != bot->GetMapId())
            ++next;
        if (next == legs.size())
        {
            Replan();
            state = MovementState::Moving;
            return;
        }
        StartLeg(next);
    }

    const NavPathSegment& leg = legs[legIndex];
    state = MovementState::Moving;
    switch (step)
    {
        case LegStep::Walk:   WalkLeg(ai, leg, now); break;
        case LegStep::Flight: TakeFlight(ai, leg, now); break;
        case LegStep::Board:  BoardTransport(ai, leg, now); break;
        case LegStep::Ride:   RideTransport(ai, leg, now); break;
        case LegStep::Portal:
            // straight into the portal's area trigger, then wait for the teleport (the map changes)
            if (!stepMs)
            {
                AreaTriggerEntry const* entry = sAreaTriggerStore.LookupEntry(leg.areaTriggerId);
                const Vector3 end = entry ? Vector3(entry->x, entry->y, entry->z) : leg.points.back();
                bot->GetMotionMaster()->MovePoint(0, end.x, end.y, end.z, FORCED_MOVEMENT_RUN, false);
                Gave(POINT_MOTION_TYPE);
                stepMs = std::max<uint32>(1, now);
            }
            else if (WorldTimer::getMSTimeDiff(stepMs, now) > portalTimeoutMs)
            {
                Replan();
                state = MovementState::Blocked;
            }
            break;
    }
}

void PlayerbotMovement::StartLeg(size_t index)
{
    legIndex = index;
    pointIndex = 0;
    pieceEnd = 0;
    walking = false;
    step = LegStep::Walk;
    stepMs = 0;
    tookOff = false;
}

void PlayerbotMovement::Replan()
{
    pathRequest.reset();
    StartLeg(0);
}

void PlayerbotMovement::WalkLeg(PlayerbotAI* ai, const NavPathSegment& leg, uint32 now)
{
    Player* bot = ai->GetBot();
    MotionMaster& motion = *bot->GetMotionMaster();
    const MovementGeneratorType current = motion.GetCurrentMovementGeneratorType();
    if (walking && ownActive && current == ownType && current == PATH_MOTION_TYPE)
        return;

    // the piece is walked: on to the next one, unless the bot was pushed off the path
    if (walking)
    {
        walking = false;
        const Vector3& end = leg.points[pieceEnd];
        if (pieceEnd + 1 < leg.points.size() && bot->GetDistance(end.x, end.y, end.z) > offPathDistance)
        {
            Replan();
            return;
        }
        pointIndex = pieceEnd;
    }

    if (pointIndex + 1 < leg.points.size())
    {
        pieceEnd = PathSubsystem::SplinePieceEnd(leg.points, pointIndex);
        std::vector<G3D::Vector3> points(leg.points.begin() + pointIndex + 1, leg.points.begin() + pieceEnd + 1);
        // from the piece's start, unless the bot stands there (the first leg starts where it stood)
        const Vector3& from = leg.points[pointIndex];
        if (bot->GetDistance(from.x, from.y, from.z) > 2.0f)
            points.insert(points.begin(), from);
        // the worker has no unit to put the points on the ground with
        if (sWorld.getConfig(CONFIG_BOOL_PATH_FIND_NORMALIZE_Z))
            for (G3D::Vector3& point : points)
                bot->UpdateAllowedPositionZ(point.x, point.y, point.z);
        motion.MovePath(points, FORCED_MOVEMENT_RUN, false, false);
        Gave(PATH_MOTION_TYPE);
        walking = true;
        return;
    }

    // the leg is walked: what ends it
    stepMs = 0;
    switch (leg.end)
    {
        // the path ended short (an incomplete navmesh path): plan again from here
        case NavLegEnd::Arrive:      Replan(); break;
        case NavLegEnd::AreaTrigger: step = LegStep::Portal; break;
        case NavLegEnd::Flight:      step = LegStep::Flight; break;
        case NavLegEnd::Transport:   step = LegStep::Board; stepMs = std::max<uint32>(1, now); break;
    }
}

void PlayerbotMovement::TakeFlight(PlayerbotAI* ai, const NavPathSegment& leg, uint32 now)
{
    Player* bot = ai->GetBot();
    // landed (Drive leaves the bot alone while it flies): on to the next leg
    if (tookOff)
    {
        StartLeg(legIndex + 1);
        return;
    }
    if (stepMs)
    {
        if (WorldTimer::getMSTimeDiff(stepMs, now) > flightTimeoutMs)
        {
            noFlightsUntilMs = now + noFlightsMs;
            Replan();
            state = MovementState::Blocked;
        }
        return;
    }

    Creature* master = FindFlightMaster(bot);
    if (!master || leg.taxiNodes.size() < 2)
    {
        noFlightsUntilMs = now + noFlightsMs;
        Replan();
        state = MovementState::Blocked;
        return;
    }
    StopOwn(bot);
    // as the client: talking to the flight master learns its node, then the flight over the nodes
    bot->GetSession()->SendLearnNewTaxiNode(master);
    WorldPacket packet(CMSG_ACTIVATETAXIEXPRESS, 8 + 4 + 4 + 4 * leg.taxiNodes.size());
    packet << master->GetObjectGuid() << uint32(0) << uint32(leg.taxiNodes.size());
    for (uint32 node : leg.taxiNodes)
        packet << node;
    bot->GetSession()->HandleActivateTaxiExpressOpcode(packet);
    stepMs = std::max<uint32>(1, now);
}

void PlayerbotMovement::BoardTransport(PlayerbotAI* ai, const NavPathSegment& leg, uint32 now)
{
    Player* bot = ai->GetBot();
    GenericTransport* transport = FindTransport(bot, leg.transportEntry);
    if (!transport || Distance(transport, leg.boardPoint) > dockedDistance)
    {
        // waiting on the dock
        if (WorldTimer::getMSTimeDiff(stepMs, now) > transportWaitMs)
        {
            Replan();
            state = MovementState::Blocked;
        }
        return;
    }

    // an elevator: the bot walks onto its platform from beside the shaft, and is on it once it stands there; a boat is
    // boarded from its dock where the bot stands (it keeps its place beside the deck)
    if (!dynamic_cast<Transport*>(transport) && bot->GetDistance2d(transport->GetPositionX(), transport->GetPositionY(), DIST_CALC_NONE) > platformDistance)
    {
        MotionMaster& motion = *bot->GetMotionMaster();
        if (!(ownActive && motion.GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE))
        {
            motion.MovePoint(0, transport->GetPositionX(), transport->GetPositionY(), transport->GetPositionZ() + 0.5f, FORCED_MOVEMENT_RUN, false);
            Gave(POINT_MOTION_TYPE);
        }
        return;
    }
    StopOwn(bot);
    transport->AddPassenger(bot, true);
    bot->NearTeleportTo(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation());
    step = LegStep::Ride;
    stepMs = std::max<uint32>(1, now);
}

void PlayerbotMovement::RideTransport(PlayerbotAI* ai, const NavPathSegment& leg, uint32 /*now*/)
{
    Player* bot = ai->GetBot();
    GenericTransport* transport = bot->GetTransport();
    // fell off, or the transport is gone
    if (!transport)
    {
        Replan();
        return;
    }
    if (bot->GetMapId() != leg.leaveMapId || Distance(transport, leg.leavePoint) > dockedDistance || legIndex + 1 >= pathRequest->segments.size())
        return;

    // arrived: off onto the next leg's start (the dock), not into the water beside the boat
    const NavPathSegment& next = pathRequest->segments[legIndex + 1];
    const Vector3 off = next.points.empty() ? leg.leavePoint : next.points.front();
    transport->RemovePassenger(bot);
    bot->NearTeleportTo(off.x, off.y, off.z, bot->GetOrientation(), false, true);
    StartLeg(legIndex + 1);
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
        // the path comes from the PathSubsystem
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
