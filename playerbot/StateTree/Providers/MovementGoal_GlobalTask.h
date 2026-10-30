#pragma once

#include "playerbot/PlayerbotMovement.h"
#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of MovementGoal_GlobalTask: the goal's spot (bound) and how it stands
    struct MovementGoal_GlobalTaskContext : public STAI_TaskContext
    {
        // bound, one by kind: location (location), position (position), range (target)
        STAI_Location location;
        STAI_Position position;
        ObjectGuid target;
        // bound, optional: why the goal wants its spot, shown after the goal in its source ("<goal> (<reason>)"),
        // e.g. InstanceProvider_GlobalTask::stagePositionFrom (the stage point saved by rtsc, the stage file's)
        std::string reason;

        // fired when the goal started or stopped driving the bot:
        //   <Transition type="OnDelegate" source="Root|MovementGoal_GlobalTask[Follow Master]::onGoalChanged" node="Root"/>
        FSTAI_Delegate onGoalChanged;
        // fired when inRange turned true
        FSTAI_Delegate onArrived;
        // fired when what the movement does changed ("moving" -> "reached", ...), whichever goal drives:
        //   <Transition type="OnDelegate" source="Root|MovementGoal_GlobalTask[Follow Master]::onMovementChanged" node="Root"/>
        FSTAI_Delegate onMovementChanged;

        // properties:
        //   wanted: the goal's conditions pass and it has its spot (it is submitted to the bot's movement)
        //   active: it is the goal that drives the bot (the highest priority of the wanted goals)
        //   inRange: the bot is where the goal wants it (location / position: within the radius; range: in range and
        //     sight of the target), also while another goal drives; the nodes using the spot check it
        //   activeGoal / activeSource: the goal that drives the bot now, whichever it is (source "<goal name> (<reason>)"; Stay
        //     without a wanted goal)
        //   movement: what the bot's movement does ("moving", "reached", "blocked", "yielding", "stopped", "none" while dead),
        //     e.g. eat only while "reached": <Condition binding="Root|MovementGoal_GlobalTask[Follow Master]::movement"
        //     compare="equal" value="reached"/>
        bool wanted = false;
        bool active = false;
        bool inRange = false;
        std::string activeGoal;
        std::string activeSource;
        std::string movement;
    };

    // A movement goal of a tree: what the bot's movement (PlayerbotMovement) does while its conditions pass. The trees
    // only say where the bot should be; the movement drives the goal with the highest priority of the trees that run
    // (the main state's tree and its subtrees, the Reaction tree) and is the only code that moves the bot.
    //   <Global>
    //     <Task class="MovementGoal_GlobalTask" name="Follow Master" priority="10" kind="location">
    //       <Radius value="follow"/>          location / position: arrived within (yards or a range name, default 2)
    //       <Binding local="location" external="Root|MasterDistance_GlobalTask::location"/>
    //       <Conditions> <Condition binding="Root|InstanceProvider_GlobalTask::masterStageCleared"/> </Conditions>
    //     </Task>
    //     <Task class="MovementGoal_GlobalTask" name="Melee" priority="50" kind="range">
    //       <Range max="melee"/>              range: max yards, a range name (spell, heal, ...) or melee; min="..." yards
    //       <Side value="behind"/>            range: any (default), behind (its back, unless it attacks the bot), front
    //       <Sight value="true"/>             range: needs line of sight (default true)
    //       <Dead value="true"/>              range: the target is dead (a body or a corpse to resurrect), default false
    //       <Binding local="target" external="Root|TargetProvider_GlobalTask::target"/>
    //     </Task>
    //     <Task class="MovementGoal_GlobalTask" name="Escape Hazard" priority="100" kind="position" interruptCast="true">
    //       <Radius value="1"/>
    //       <Binding local="position" external="Root|HazardProvider_GlobalTask::safePosition"/>
    //       <Conditions> <Condition binding="Root|HazardProvider_GlobalTask::inHazard"/> </Conditions>
    //     </Task>
    //   </Global>
    // kind: location (STAI_Location, any map: a path across dungeon portals), position (STAI_Position on the bot's map),
    // range (a unit or a corpse). interruptCast="true": moves also while the bot casts. Ticked: submits the goal every
    // update while its conditions pass and its spot is set; the movement reports back. A node reads the goal by name:
    //   <Condition binding="Root|MovementGoal_GlobalTask[Heal Range]::inRange"/>
    class MovementGoal_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Settings
        {
            std::string name;
            int32 priority = 0;
            MovementGoalKind kind = MovementGoalKind::Position;
            std::string radius = "2";       // yards or a range name
            std::string maxRange;           // range: yards, a range name or "melee"
            float minRange = 0.0f;
            MovementSide side = MovementSide::Any;
            bool sight = true;
            bool dead = false;
            bool interruptCast = false;
        };

        explicit MovementGoal_GlobalTask(Settings settings) : settings(std::move(settings)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "MovementGoal_GlobalTask"; }
        inline static std::string task_name = "MovementGoal";
        virtual std::string GetName() const override { return task_name + " " + settings.name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<MovementGoal_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual void OnExitState(STAI_TaskContext& context) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);
        // yards of a radius / range setting (a number or a range name)
        static float ToYards(const STAI_TaskContext& context, const std::string& value);
        void Update(MovementGoal_GlobalTaskContext& context) const;

        Settings settings;
    };
}
