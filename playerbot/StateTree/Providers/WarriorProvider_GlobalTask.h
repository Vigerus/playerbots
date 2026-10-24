#pragma once

#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>

class Player;

namespace ai
{
    // the warrior stances: their spell and shapeshift form; "battle", "defensive", "berserker"
    struct STAI_WarriorStance
    {
        const char* name;
        uint32 spellId;
        uint8 form;
    };
    // the stance by name, null for an unknown name
    const STAI_WarriorStance* STAI_FindWarriorStance(const std::string& name);
    // the stance the player is in, null for none
    const STAI_WarriorStance* STAI_GetWarriorStance(const Player* player);

    // runtime context of WarriorProvider_GlobalTask: rage, stance and the reactive abilities of the bot
    struct WarriorProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when Revenge or Overpower became usable:
        //   <Transition type="OnDelegate" source="Root|WarriorProvider_GlobalTask::onReactive" node="Root"/>
        FSTAI_Delegate onReactive;

        // properties:
        //   rage: the bot's rage (0 - 100)
        //   stance: "battle", "defensive", "berserker", empty without one
        //   revenge: the bot blocked, dodged or parried lately (Revenge, defensive stance)
        //   overpower: a target of the bot dodged lately (Overpower, battle stance); overpowerTarget is that target
        //   knowsDefensive / knowsBerserker: the bot knows the stance (Defensive Stance at 10, Berserker Stance from
        //     the level 30 quest): the stance nodes switch only to a stance the bot has
        uint32 rage = 0;
        std::string stance;
        bool revenge = false;
        bool overpower = false;
        ObjectGuid overpowerTarget;
        bool knowsDefensive = false;
        bool knowsBerserker = false;

        uint32 sinceCheckMs = 0;
    };

    // Global task of the warrior trees: <Task class="WarriorProvider_GlobalTask"/>. Ticked: checks every 100ms.
    // Revenge comes from the core's AURA_STATE_DEFENSE (set by a block, dodge or parry, gone after 4s), Overpower from
    // the combo point a warrior gets when its target dodges (gone after 4s or when used).
    class WarriorProvider_GlobalTask : public STAI_GlobalTask
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<WarriorProvider_GlobalTask>(); }

        virtual const char* GetTaskClass() const override { return "WarriorProvider_GlobalTask"; }
        inline static std::string task_name = "WarriorProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<WarriorProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnter(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

    private:
        static void Update(WarriorProvider_GlobalTaskContext& context);
    };
}
