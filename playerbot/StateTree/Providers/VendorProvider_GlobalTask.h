#pragma once

#include "playerbot/StateTree/STAI_Navigation.h"
#include "playerbot/StateTree/Tasks/STAI_GlobalTask.h"

#include "Entities/ObjectGuid.h"

#include <string>
#include <vector>

namespace ai
{
    // runtime context of VendorProvider_GlobalTask
    struct VendorProvider_GlobalTaskContext : public STAI_TaskContext
    {
        STAI_Location vendorLocation;   // property "vendorLocation": the nearest vendor's spawn, unset without one
        uint32 vendorDistance = 0;      // property "vendorDistance": yards to it, 0 without one
        ObjectGuid vendor;              // property "vendor": that vendor while it is near the bot (for a range goal / Task_SellJunk)

        uint32 vendorEntry = 0;
        uint32 sinceCheckMs = 0;
        uint32 sinceChooseMs = 0;
    };

    // Global task that finds the nearest vendor on the bot's map that is not hostile to it (creature table spawns
    // within Range; any vendor buys anything):
    //   <GlobalTasks>
    //     <Task class="VendorProvider_GlobalTask">
    //       <Range value="2000"/>    yards to a vendor, default 2000
    //     </Task>
    //   </GlobalTasks>
    // Ticked: the nearest spawn chosen when the task starts (and again every 30s), the vendor near the bot every second.
    class VendorProvider_GlobalTask : public STAI_DataProviderBase
    {
    public:
        explicit VendorProvider_GlobalTask(float range) : range(range) {}

        static void Validate(const STAI_TaskConfig& config);
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config);

        virtual const char* GetNodeClass() const override { return "VendorProvider_GlobalTask"; }
        inline static std::string task_name = "VendorProvider";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<VendorProvider_GlobalTaskContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool ShouldCallTick() const override { return true; }

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

    private:
        static float ParseRange(const STAI_TaskConfig& config);
        void ChooseVendor(VendorProvider_GlobalTaskContext& context) const;
        void Update(VendorProvider_GlobalTaskContext& context) const;

        float range;
    };
}
