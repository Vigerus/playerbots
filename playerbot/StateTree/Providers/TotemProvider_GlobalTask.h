#pragma once

#include "playerbot/StateTree/Tasks/STAI_DebugSpells.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include <array>
#include <vector>

namespace ai
{
    // runtime context of TotemProvider_GlobalTask: the totems the bot keeps up and the ones that are not up
    struct TotemProvider_GlobalTaskContext : public STAI_TaskContext
    {
        // fired when the totems changed: another one selected (WoWAddonInspectBot), the master went into a stage with
        // another set, a rank learned (and on the first check):
        //   <Transition type="OnDelegate" source="Root|TotemProvider_GlobalTask::onTotemsChanged" node="Root"/>
        FSTAI_Delegate onTotemsChanged;
        // fired when a selected totem is not up any more (never cast, expired, destroyed, left behind) and it was up
        // before (and on the first check while any is not up):
        //   <Transition type="OnDelegate" source="Root|TotemProvider_GlobalTask::onTotemMissing" node="Root|Totems"/>
        FSTAI_Delegate onTotemMissing;

        // properties:
        //   fire / earth / water / air: the spell the bot casts for the school (the highest rank it knows of the
        //     selected totem), 0 none
        //   needs: those spells whose totem is not up, in school order (fire, earth, water, air); for Task_CastTotem
        //   mapId / stage: where the set in use is saved (0 / 0: the outdoor set)
        uint32 fire = 0;
        uint32 earth = 0;
        uint32 water = 0;
        uint32 air = 0;
        std::vector<uint32> needs;
        uint32 mapId = 0;
        uint32 stage = 0;

        uint32 sinceCheckMs = 0;
        uint32 seenVersion = 0;     // PlayerbotTotems::GetVersion of the last check
    };

    // Global task that keeps a shaman's totems (PlayerbotTotems: a totem or none per school, the outdoor set or the
    // set of the dungeon stage the master stands in; selected in WoWAddonInspectBot):
    //   <Global>
    //     <Task class="TotemProvider_GlobalTask">
    //       <Range value="30"/>    a totem farther from the bot than this is not up (left behind), default 30
    //     </Task>
    //   </Global>
    //   <Node name="Totems">
    //     <Conditions> <Condition binding="Root|TotemProvider_GlobalTask::needs"/> </Conditions>
    //     <Task class="Task_CastTotem">
    //       <Binding local="needs" external="Root|TotemProvider_GlobalTask::needs"/>
    //     </Task>
    //   </Node>
    // A totem is up while the bot's totem of the school was summoned by a rank of the selected spell, is alive and in
    // range. Ticked: checks every 250ms; onTotemsChanged fires when a school's spell changed, onTotemMissing when needs
    // got a spell it did not have.
    class TotemProvider_GlobalTask : public STAI_DataProviderBase, public ISTAI_DebugSpells
    {
    public:
        explicit TotemProvider_GlobalTask(float range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "TotemProvider_GlobalTask"; }
        inline static std::string task_name = "TotemProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<TotemProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override;

        virtual std::vector<uint32> GetDebugSpells(const STAI_TaskContext& context) const override;

    private:
        static float ParseRange(const STAI_TaskConfig& config);
        void Update(TotemProvider_GlobalTaskContext& context, bool first) const;

        float range;
    };
}
