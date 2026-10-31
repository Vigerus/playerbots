#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

struct CreatureInfo;
class Player;

namespace ai
{
    // runtime context of TrainerProvider_GlobalTask
    struct TrainerProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the bot got spells to learn (newSpells went above 0):
        //   <Transition trigger="OnDelegate" source="Root|TrainerProvider_GlobalTask::onNewSpells" target="Root"/>
        FSTAI_Delegate onNewSpells;
        uint32 newSpells = 0;           // property "newSpells": spells the bot can learn now and afford, cheapest first
        uint32 cost = 0;                // property "cost": what they cost together (copper, before a reputation discount)
        STAI_Location trainerLocation;  // property "trainerLocation": the nearest class trainer's spawn, unset without one
        uint32 trainerDistance = 0;     // property "trainerDistance": yards to it, 0 without one
        ObjectGuid trainer;             // property "trainer": that trainer while it is near the bot (for a range goal / Task_LearnSpells)

        uint32 trainerEntry = 0;
        uint32 mapId = 0;
        uint32 sinceCheckMs = 0;
    };

    // Global task that knows what the bot's class trainer would teach it: the nearest friendly trainer of the bot's
    // class on its map (creature table spawns, within Range) and the spells it can learn there now (the trainer's
    // green spells, as the trainer window shows them) that its money pays for, cheapest first:
    //   <GlobalTasks>
    //     <Task class="TrainerProvider_GlobalTask">
    //       <Range value="2000"/>    yards to a trainer, default 2000
    //     </Task>
    //   </GlobalTasks>
    // Ticked: checks every 5s. Task_LearnSpells buys them.
    class TrainerProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Learnable
        {
            uint32 spellId;     // the trainer spell (it teaches its learned spell)
            uint32 cost;
        };

        explicit TrainerProvider_GlobalTask(float range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        // the spells of the trainer the bot can learn now (green), cheapest first, money not checked
        static std::vector<Learnable> FindLearnable(Player* bot, const CreatureInfo* trainer);
        // a trainer of the bot's class that teaches it (not hostile to it)
        static bool IsClassTrainerFor(const Player* bot, const CreatureInfo* info);

        virtual const char* GetNodeClass() const override { return "TrainerProvider_GlobalTask"; }
        inline static std::string task_name = "TrainerProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TrainerProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static float ParseRange(const STAI_TaskConfig& config);
        void Update(TrainerProvider_GlobalTaskContext& context) const;

        float range;
    };
}
