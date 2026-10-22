#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <memory>
#include <string>

namespace ai
{
    // runtime context of Task_MakePath
    struct Task_MakePathContext : public STAI_TaskContext
    {
        STAI_Location location;     // property "location": where to go, bound or the task's own <Location>
        STAI_Path path;             // property "path": the result, for Task_MoveTo
        std::shared_ptr<NavPathRequest> request;
        uint32 elapsedMs = 0;
    };

    // Calculates the path from the bot to a location on the PathfindingWorker's threads:
    //   <Task class="Task_MakePath">
    //     <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
    //   </Task>
    // or a fixed spot: <Location map="289" x="..." y="..." z="..."/> (map optional, default the bot's map).
    // The maps are one graph: on another map the path walks to the portal(s) on the way (the teleport area triggers
    // are its navlinks). Running until the path is there; Succeeded with property "path" set (bind it to Task_MoveTo),
    // Failed without a location, when no way leads there (another continent without a portal, no navmesh way from
    // the bot) or after 10s.
    class Task_MakePath : public STAI_Task
    {
    public:
        Task_MakePath(const STAI_Location& fixedLocation, const std::string& locationSource)
            : fixedLocation(fixedLocation), locationSource(locationSource) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "Task_MakePath"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_MakePathContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override;

    private:
        STAI_Location fixedLocation;    // <Location>, unset when the location is bound
        std::string locationSource;     // what the location is bound to, for the debug info
    };
}
