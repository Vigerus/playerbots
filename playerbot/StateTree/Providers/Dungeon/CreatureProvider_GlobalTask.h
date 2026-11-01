#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

class Creature;
class Player;

namespace ai
{
    // runtime context of CreatureProvider_GlobalTask: the watched creatures around the bot
    struct CreatureProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when a watched creature appeared that was not there at the last check:
        //   <Transition type="OnDelegate" source="Root|CreatureProvider_GlobalTask::onAppeared" node="..."/>
        FSTAI_Delegate onAppeared;
        // fired when the last watched creature died or left the range
        FSTAI_Delegate onGone;
        // the living, hostile watched creatures in range, the closest first
        std::vector<ObjectGuid> units;
        // the first of units, for a range movement goal or Task_CastSpell:
        //   <Binding local="target" external="Root|CreatureProvider_GlobalTask::closest"/>
        ObjectGuid closest;
        uint32 count = 0;

        uint32 sinceCheckMs = 0;
    };

    // Global task that watches creatures by entry around the bot (an encounter's adds, a boss's illusions):
    //   <Global>
    //     <Task class="CreatureProvider_GlobalTask">
    //       <Creature entry="11439"/>    per entry, one or more
    //       <Range value="40"/>          yards, default 40
    //     </Task>
    //   </Global>
    // Ticked: checks every 250ms.
    class CreatureProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        CreatureProvider_GlobalTask(std::vector<uint32> entries, float range) : entries(std::move(entries)), range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "CreatureProvider_GlobalTask"; }
        inline static std::string task_name = "CreatureProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<CreatureProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        // the living creatures of the entries within range of the bot that are hostile to it, the closest first
        static std::vector<Creature*> FindCreatures(Player* bot, const std::vector<uint32>& entries, float range);
        // "11439" or "11439,11440": the entries, each a known creature; throws on anything else
        static std::vector<uint32> ParseEntries(const std::string& text);
        // <Range value="..."/>: yards, default 40
        static float ParseRange(const STAI_TaskConfig& config);

    private:
        static std::vector<uint32> ParseCreatures(const STAI_TaskConfig& config);
        void Update(CreatureProvider_GlobalTaskContext& context) const;

        std::vector<uint32> entries;
        float range;
    };
}
