#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Player;
class Quest;

namespace ai
{
    struct STAI_SpawnPoint;

    // runtime context of QuestProvider_GlobalTask
    struct QuestProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the quest log changed (a quest accepted, an objective counted, a quest completed or turned in):
        //   <Transition trigger="OnDelegate" source="Root|QuestProvider_GlobalTask::onQuestsChanged" target="Root"/>
        FSTAI_Delegate onQuestsChanged;

        bool pickUp = false;            // property "pickUp": go to giverLocation and accept its quests now
        uint32 available = 0;           // property "available": quests the chosen giver offers the bot
        STAI_Location giverLocation;    // property "giverLocation": the nearest giver spawn with quests for the bot
        ObjectGuid giver;               // property "giver": that giver while it is near the bot

        bool handIn = false;            // property "handIn": go to takerLocation and turn in now (see the batching below)
        uint32 completed = 0;           // property "completed": completed quests in the log
        STAI_Location takerLocation;    // property "takerLocation": the nearest spawn a completed quest is turned in at
        ObjectGuid taker;               // property "taker": that creature while it is near the bot

        uint32 open = 0;                // property "open": incomplete quests in the log the bot can work on
        uint32 logFree = 0;             // property "logFree": free quest log slots
        std::string quest;              // property "quest": the quest worked on (the nearest objective)
        STAI_Location objectiveLocation;            // property "objectiveLocation": the next spawn point of its objective
        std::vector<uint32> objectiveEntries;       // property "objectiveEntries": creatures the open quests need killed or looted
        ObjectGuid objectTarget;        // property "objectTarget": a game object near the bot a quest needs (sparkling)

        struct Spot
        {
            uint32 entry;
            float x, y;
            uint32 untilMs;
        };
        std::vector<Spot> ignored;      // givers / takers the bot stood at without getting anything done
        std::vector<Spot> visited;      // objective spawn points reached lately
        std::vector<std::pair<ObjectGuid, uint32>> ignoredObjects;
        uint32 giverEntry = 0;
        uint32 takerEntry = 0;
        uint32 nearGiverMs = 0;         // how long the bot has been at the chosen giver / taker
        uint32 nearTakerMs = 0;
        uint32 objectSinceMs = 0;
        uint32 logHash = 0;
        uint32 sinceCheckMs = 0;
        uint32 nowMs = 0;
    };

    // Global task that plans a solo bot's questing from its quest log and the quest givers of its map:
    //   - pick up: the nearest creature (spawn within Range) offering a quest the bot can take, of a fitting level, for
    //     one player, without a timer, an escort or an area / event objective, whose objectives and taker exist on the
    //     bot's map; pickUp while the log has room and the giver is within Near, or there is nothing open to work on
    //   - objectives: for every incomplete quest the creatures to kill, the creatures / game objects dropping its items
    //     and the game objects to use; the quest whose next unvisited spawn point is nearest is worked on (nearest
    //     first is the order quests are done in); objectiveEntries holds every open quest's creatures, so a kill on
    //     the way counts too
    //   - hand in: the nearest taker of a completed quest; handIn once no open quest is turned in at the same place
    //     (takers within Hub of it), so quests of one hub are handed in together, or when the taker is within Near, or
    //     nothing is left to work on
    // A giver or taker the bot stayed within 30 yards of for 30s while it stayed chosen is ignored for 10 minutes.
    //   <GlobalTasks>
    //     <Task class="QuestProvider_GlobalTask">
    //       <Range value="1500"/>              yards to a giver, default 1500
    //       <Levels below="5" above="2"/>      quest levels around the bot's, default 5 below and 2 above
    //       <Near value="80"/>                 yards: a giver / taker this close is visited at once, default 80
    //       <Hub value="60"/>                  yards between takers of one place, default 60
    //       <ObjectRange value="40"/>          yards searched for game objects to use, default 40
    //     </Task>
    //   </GlobalTasks>
    // Ticked: checks every second. Task_AcceptQuests, Task_TurnInQuests and Task_UseObject act on its guids.
    class QuestProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        struct Settings
        {
            float range = 1500.0f;
            uint32 levelsBelow = 5;
            uint32 levelsAbove = 2;
            float nearRange = 80.0f;
            float hub = 60.0f;
            float objectRange = 40.0f;
        };

        explicit QuestProvider_GlobalTask(const Settings& settings) : settings(settings) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        // a quest of the creature the bot should take now (fitting level, doable solo on its map, CanTakeQuest)
        static bool IsWanted(Player* bot, const Quest* quest, uint32 levelsBelow, uint32 levelsAbove);

        virtual const char* GetNodeClass() const override { return "QuestProvider_GlobalTask"; }
        inline static std::string task_name = "QuestProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<QuestProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static Settings ParseSettings(const STAI_TaskConfig& config);

        void Update(QuestProvider_GlobalTaskContext& context, uint32 elapsed) const;
        void UpdateObjectTarget(QuestProvider_GlobalTaskContext& context, uint32 elapsed) const;

        Settings settings;
    };
}
