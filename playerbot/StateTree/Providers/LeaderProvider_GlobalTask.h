#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of LeaderProvider_GlobalTask
    struct LeaderProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired on every bot update while the leader is further away than the range:
        //   <Transition trigger="OnDelegate" source="Root|LeaderProvider_GlobalTask::onLeaderFar" target="Root|Follow"/>
        FSTAI_Delegate onLeaderFar;
        FSTAI_Delegate onLeaderChanged;     // another leader, or none any more
        ObjectGuid leader;                  // property "leader": the player the bot follows, empty without one
        bool hasLeader = false;             // property "hasLeader"
        bool leaderIsMaster = false;        // property "leaderIsMaster": the leader is the bot's master (not only the group leader)
        bool leaderFar = false;             // property "leaderFar"
        // property "location": where the leader is (unset without one or while it is being teleported), for
        // <Binding local="location" external="Root|LeaderProvider_GlobalTask::location"/> of a location movement goal
        STAI_Location location;
    };

    // Global task: the player the bot follows in its group, a human or a bot alike. Only grouped bots have a leader:
    // the bot's master while the master is in the bot's group (the master has the higher authority), else the group
    // leader; none while the bot is solo or leads the group itself. A living leader on another map or instance is
    // always far (none of them while one is being teleported):
    //   <GlobalTasks>
    //     <Task class="LeaderProvider_GlobalTask">
    //       <Range value="follow"/>     yards or a range name (default follow)
    //     </Task>
    //   </GlobalTasks>
    // Tickable: fires onLeaderFar every update while the leader is further away, onLeaderChanged when it changed.
    class LeaderProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        explicit LeaderProvider_GlobalTask(const std::string& range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        // the leader of the bot as described above, nullptr without one
        static Player* FindLeader(PlayerbotAI* ai, Player* bot);

        virtual const char* GetNodeClass() const override { return "LeaderProvider_GlobalTask"; }
        inline static std::string task_name = "LeaderProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<LeaderProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;


    private:
        std::string range;      // yards or a range name
    };
}
