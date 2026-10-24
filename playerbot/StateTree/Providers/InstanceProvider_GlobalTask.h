#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"
#include "playerbot/StateTree/STAI_Navigation.h"

#include <string>
#include <vector>

class Map;
class Player;

namespace ai
{
    // runtime context of InstanceProvider_GlobalTask: the bot's subscription, its current map / instance and the
    // stages (rooms, InstanceSubsystem) the bot and its master stand in
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

        // fired when the stage the master stands in got cleared (its last creature died):
        //   <Transition type="OnDelegate" source="Root|InstanceProvider_GlobalTask::onStageCleared" node="Root|Follow player"/>
        FSTAI_Delegate onStageCleared;
        // fired when following / going to the stage position got blocked: the master went into a stage that is not
        // cleared and has no uncleared point for the bot, or out of every stage, or the bot's way crosses a stage that
        // is not cleared (pathBlocked)
        //   <Transition type="OnDelegate" source="Root|InstanceProvider_GlobalTask::onStageBlocked" node="Root|UtilityAI"/>
        FSTAI_Delegate onStageBlocked;
        // fired on every check while the bot is away from its stagePosition (and its way there is not blocked)
        //   <Transition type="OnDelegate" source="Root|InstanceProvider_GlobalTask::onStagePositionFar" node="Root|Position Yourself"/>
        FSTAI_Delegate onStagePositionFar;

        // properties:
        //   stage / masterStage: index of the stage the bot / master stands in, -1 none or no stages on the map
        //   masterStageCleared: the master stands in a cleared stage, the bot may follow it (true while the master is
        //     not on the bot's map or the map has no stages; false while it stands outside every stage)
        //   pathBlocked: the bot's way to its destination (stagePosition, else the master) crosses a stage that is
        //     not cleared (other than the one the bot is in and the master's); only checked while the bot is away
        //   stagePosition: the bot's point in the master's stage, the cleared one once the stage is cleared, else the
        //     uncleared one (the bot's override from rtsc / its first arrival at the entrance, else the stage's
        //     position for its class / spec); unset without one
        //   stagePositionFar: stagePosition is set and the bot is more than 2 yards away
        //   stagePositionFrom: where stagePosition comes from: "<stage name> cleared|uncleared, rtsc" (the bot's override
        //     saved by rtsc), "..., entrance" (its first arrival, stage 0), "..., stage file" (the stage's position);
        //     empty without one (the movement goal's reason)
        int32 stage = -1;
        int32 masterStage = -1;
        bool masterStageCleared = true;
        bool pathBlocked = false;
        STAI_Position stagePosition;
        std::string stagePositionFrom;
        bool stagePositionFar = false;

        bool hasStages = false;     // the map has stages
        uint32 stageTimer = 0;      // ms since the stages were checked

        // the bot's entrance points (stage 0) are saved from entrance by the next UpdateStages when it has none
        bool entrancePending = false;
        STAI_Position entrance;

        bool subscribed = false;

        void Subscribe();
        void Unsubscribe();
        // takes the map over, fires when it is another map / instance than the known one. arrived: the bot was just
        // added to the map (else it is seen there first when the tree starts)
        void SetMap(Map* map, bool arrived);

        void OnPlayerEnterMap(Player* player, Map* map);
        // finds the stages of the bot and its master, the stage position and whether the way is blocked; fires
        // onStageCleared / onStageBlocked / onStagePositionFar
        void UpdateStages();
    };

    // Global task that tells the tree when the bot changed map or instance:
    //   <Global> <Task class="InstanceProvider_GlobalTask"/> </Global>
    // Map changes come from the InstanceSubsystem (Map::Add of the bot). The last map is kept while the tree is
    // not running (combat, death), so a change in between fires when the tree starts again.
    // Ticked for the stages (every 500ms, only on maps with stages), e.g. a follow node that does not walk into or
    // through rooms with living creatures:
    //   <Condition binding="Root|InstanceProvider_GlobalTask::masterStageCleared"/>
    //   <Condition binding="Root|InstanceProvider_GlobalTask::pathBlocked" negate="true"/>
    class InstanceProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<InstanceProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "InstanceProvider_GlobalTask"; }
        inline static std::string task_name = "InstanceProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<InstanceProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual void OnExit(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    };
}
