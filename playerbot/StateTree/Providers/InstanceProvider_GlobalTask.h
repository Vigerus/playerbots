#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>
#include <vector>

class Map;
class Player;

namespace ai
{
    // runtime context of InstanceProvider_GlobalTask: the bot's subscription and its current map / instance
    struct InstanceProvider_GlobalTaskContext : public STAI_TaskContext
    {
        ~InstanceProvider_GlobalTaskContext() override { Unsubscribe(); }

        // fired when the bot is on another map or instance than before:
        //   <Transition type="OnDelegate" source="Root|InstanceProvider_GlobalTask::onInstanceChanged" node="..."/>
        FSTAI_Delegate onInstanceChanged;
        // properties mapId, instanceId, inDungeon (dungeon, raid or battleground instance)
        uint32 mapId = 0;
        uint32 instanceId = 0;
        bool inDungeon = false;
        bool known = false;         // mapId / instanceId were set once

        bool subscribed = false;

        void Subscribe();
        void Unsubscribe();
        // takes the map over, fires when it is another map / instance than the known one
        void SetMap(Map* map);

        void OnPlayerEnterMap(Player* player, Map* map);
    };

    // Global task that tells the tree when the bot changed map or instance:
    //   <Global> <Task class="InstanceProvider_GlobalTask"/> </Global>
    // Not tickable: it reacts to the InstanceSubsystem (Map::Add of the bot). The last map is kept while the tree is
    // not running (combat, death), so a change in between fires when the tree starts again.
    class InstanceProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<InstanceProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "InstanceProvider_GlobalTask"; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<InstanceProvider_GlobalTaskContext>(); }

        virtual void OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::string GetDebugInfo(const STAI_TaskContext& context) const override;
    };
}
