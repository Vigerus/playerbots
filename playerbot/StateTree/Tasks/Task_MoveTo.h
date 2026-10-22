#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <string>

namespace ai
{
    // runtime context of Task_MoveTo
    struct Task_MoveToContext : public STAI_TaskContext
    {
        STAI_Path path;                 // property "path", bound to Task_MakePath::path
        STAI_Location location;         // property "location", optional: where the destination is now
        uint32 mapId = 0;               // the map the bot walked on when the task started
        bool moving = false;            // a move ordered by this task is in progress
        bool pushed = false;            // walked straight into the navlink after the path ended outside it
        bool keepMoving = false;        // left to plan again: the move goes on until the new path is there
        uint32 elapsedMs = 0;
        uint32 timeoutMs = 0;
        float startDistance = 0.0f;     // to the destination when the task started
    };

    // Walks a path made by Task_MakePath and stops at the destination:
    //   <Task class="Task_MoveTo">
    //     <AcceptableRadius value="follow"/>   yards, or a range name ("follow": the bot's follow distance)
    //     <Binding local="path" external="Root|Follow player|Task_MakePath::path"/>
    //     <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>   optional
    //   </Task>
    // Walks the leg of the path on the bot's map. A leg that ends in a navlink (dungeon portal) walks into it: the
    // bot enters the area trigger like a player and is teleported (PlayerbotAI::UpdateAreaTriggers); the task
    // succeeds once the bot is on the other side (the node plans again on the new map's navmesh).
    // Succeeded inside the radius of the destination, when the bound location moved further than the radius from
    // the destination (the node plans again, the bot keeps moving meanwhile), or when the path ended short of the
    // destination but brought the bot closer (a long path cut at its length limit). Failed without a path, when it
    // ended without bringing the bot closer, or after the path's walking time plus 10s.
    // OnExit stops the movement, unless the task finished to plan again.
    class Task_MoveTo : public STAI_Task
    {
    public:
        Task_MoveTo(const std::string& acceptableRadius, const std::string& pathSource)
            : acceptableRadius(acceptableRadius), pathSource(pathSource) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_MoveTo"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_MoveToContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Move along " + pathSource + ", radius " + acceptableRadius; }

    private:
        float GetRadius(const STAI_TaskContext& context) const;

        std::string acceptableRadius;   // yards or a range name
        std::string pathSource;         // what the path is bound to, for the debug info
    };
}
