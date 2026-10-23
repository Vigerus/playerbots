#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <string>

class Player;

namespace ai
{
    // runtime context of SpecProvider_GlobalTask: the bot's spec
    struct SpecProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the spec changed (talents learned or reset), and on the first check:
        //   <Transition type="OnDelegate" source="Root|SpecProvider_GlobalTask::onSpecChanged" node="Root"/>
        FSTAI_Delegate onSpecChanged;
        // the bot's spec, compared as text:
        //   <Condition binding="Root|SpecProvider_GlobalTask::specName" compare="equal" value="frost mage"/>
        std::string specName;

        uint32 sinceCheckMs = 0;
    };

    // Global task that names the bot's spec from its talent tab with the most points (PlayerSpec; a class default
    // below level 10):
    //   <Global>
    //     <Task class="SpecProvider_GlobalTask"/>
    //   </Global>
    // specName is one of: "frost mage", "fire mage" (arcane counts as frost), "holy priest", "disc priest",
    // "shadow priest", "warlock", "rogue", "hunter", "elem shaman", "enh shaman", "resto shaman",
    // "prot druid" (balance and feral), "resto druid", "holy paladin", "prot paladin", "retri paladin",
    // "arms warrior", "fury warrior", "prot warrior"; empty for other classes. Ticked: checks every 2s;
    // onSpecChanged fires when the name changed.
    class SpecProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<SpecProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "SpecProvider_GlobalTask"; }
        inline static std::string task_name = "SpecProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<SpecProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        // the spec name of the player (see above)
        static std::string GetSpecName(const Player* player);

    private:
        static void Update(SpecProvider_GlobalTaskContext& context);
    };
}
