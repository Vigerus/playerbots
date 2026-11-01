#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"

#include <string>

namespace ai
{
    // runtime context of Task_SellJunk: the vendor (bound) and the run
    struct Task_SellJunkContext : public STAI_TaskContext
    {
        ObjectGuid vendor;          // property "vendor", e.g. bound to VendorProvider_GlobalTask::vendor
        std::string result;         // how the run went, in its History entry
    };

    // Sells the bag items ItemEvalSubsystem says to sell to a vendor (EItemEval::Vendor), as a player dragging the
    // items onto the vendor window (CMSG_SELL_ITEM: the core checks the distance and pays). The tree's movement goal
    // (range on the vendor) brings the bot there:
    //   <Task class="Task_SellJunk">
    //     <Binding local="vendor" external="Root|VendorProvider_GlobalTask::vendor"/>
    //   </Task>
    // Not tickable: Succeeded in OnEnterState when it sold something or nothing is left to sell, Failed otherwise (out of reach);
    // its History entry tells how many items and how much money.
    class Task_SellJunk : public STAI_TaskBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_SellJunk>(); }

        virtual const char* GetNodeClass() const override { return "Task_SellJunk"; }
        inline static std::string task_name = "SellJunk";
        virtual std::string GetName() const override { return task_name; }

        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Task_SellJunkContext>(); }

        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override;

        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override;

        virtual std::string GetDebugInfo() const override { return "Sell junk at the vendor"; }
        virtual std::string GetActivityInfo(const STAI_TaskContext& context) const override;
    };
}
