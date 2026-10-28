#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of EncounterProvider_GlobalTask: the encounter tactic that applies now
    struct EncounterProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when tactic changed (also to none):
        //   <Transition type="OnDelegate" source="Root|EncounterProvider_GlobalTask::onTacticChanged" node="Root"/>
        FSTAI_Delegate onTacticChanged;
        // the name of the tactic, empty for none:
        //   <Condition binding="Root|EncounterProvider_GlobalTask::tactic" compare="equal" value="jandice illusions"/>
        std::string tactic;

        uint32 sinceCheckMs = 0;
    };

    // Global task that names the encounter tactic the class tree runs instead of its spec subtree. The trees keep the
    // boss knowledge: a tactic applies while the bot is on its map and a living, hostile creature of its entries is
    // within its range of the bot (CreatureProvider_GlobalTask::FindCreatures); the first listed one that applies wins:
    //   <Global>
    //     <Task class="EncounterProvider_GlobalTask">
    //       <Tactic name="jandice illusions" map="289" entry="11439" range="40"/>   entry: comma separated; range: yards, default 40
    //     </Task>
    //   </Global>
    // Ticked: checks every 250ms.
    class EncounterProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        struct Tactic
        {
            std::string name;
            uint32 mapId = 0;
            std::vector<uint32> entries;
            float range = 40.0f;
        };

        explicit EncounterProvider_GlobalTask(std::vector<Tactic> tactics) : tactics(std::move(tactics)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config);

        virtual const char* GetTaskClass() const override { return "EncounterProvider_GlobalTask"; }
        inline static std::string task_name = "EncounterProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<EncounterProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static std::vector<Tactic> ParseTactics(const STAI_TaskConfig& config);
        void Update(EncounterProvider_GlobalTaskContext& context) const;

        std::vector<Tactic> tactics;
    };
}
