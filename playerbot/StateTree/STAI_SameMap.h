#pragma once

#include "Entities/Object.h"

namespace ai
{
    // both objects are in the world on the same map (and instance). Unlike a->GetMap() == b->GetMap() it never
    // asserts: GetMap() asserts on an object out of the world (a bot in a far teleport, logging out), and the aura /
    // team events of group members call in from the member's map thread, where the bot may be just that.
    inline bool IsOnSameMap(const WorldObject* a, const WorldObject* b)
    {
        return a && b && a->IsInWorld() && b->IsInWorld() && a->GetMapId() == b->GetMapId() && a->GetInstanceId() == b->GetInstanceId();
    }
}
