#pragma once

#include "MotionGenerators/PathfindingWorker.h"

#include <memory>

namespace ai
{
    // a spot on a map, as a task property (MasterDistance_GlobalTask::location, Task_MakePath::location); unset
    // while !valid
    struct STAI_Location
    {
        uint32 mapId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool valid = false;

        bool IsEmpty() const { return !valid; }
        bool operator==(const STAI_Location& other) const { return valid == other.valid && mapId == other.mapId && x == other.x && y == other.y && z == other.z; }
        bool operator!=(const STAI_Location& other) const { return !(*this == other); }
    };

    // a path calculated by the PathfindingWorker (Task_MakePath::path, walked by Task_MoveTo): its legs on each
    // map, joined by navlinks (dungeon portals); shared, so binding it copies no points. Unset until it is ready.
    struct STAI_Path
    {
        std::shared_ptr<const NavPathRequest> request;

        bool IsEmpty() const { return !request || !request->IsReady() || request->segments.empty(); }
        bool operator==(const STAI_Path& other) const { return request == other.request; }
        bool operator!=(const STAI_Path& other) const { return !(*this == other); }
    };
}
