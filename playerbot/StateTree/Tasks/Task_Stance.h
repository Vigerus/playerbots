#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"
#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"

#include <string>
#include <vector>

namespace ai
{
    // Puts the warrior into a stance:
    //   <Task class="Task_Stance">
    //     <Stance value="berserker"/>   battle, defensive or berserker
    //     <Stance value="battle"/>      more: the first one the bot knows (Berserker Stance comes with a quest at 30)
    //   </Task>
    // Not tickable: Succeeded in OnEnterState when the bot is in the first stance it knows (also when it already was),
    // Failed when it knows none of them or the switch did not happen (the stance cooldown).
    class Task_Stance : public STAI_TaskBase, public ISTAI_DebugSpells
    {
    public:
        explicit Task_Stance(std::vector<std::string> stances) : stances(std::move(stances)) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "Task_Stance"; }
        inline static std::string task_name = "Stance";
        virtual std::string GetName() const override { return task_name; }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual std::string GetDebugInfo() const override;
        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

    private:
        static std::vector<std::string> ParseStances(const STAI_TaskConfig& config);

        std::vector<std::string> stances;
    };
}
