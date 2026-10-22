#pragma once

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include <memory>

namespace ai
{
    // Opens the bot's trade window for the player that opened a trade with it (the deprecated TradeStatusAction on
    // TRADE_STATUS_BEGIN_TRADE): faces the trader and begins the trade, or cancels it when the bot does not trade with
    // that player (STAI_CanTradeWith: other bots, the master, group members).
    //   <Task class="Task_OpenTrade"/>
    // Succeeded when the window was opened or there is no trade, Failed when the trade was cancelled.
    class Task_OpenTrade : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_OpenTrade>(); }

        virtual const char* GetTaskClass() const override { return "Task_OpenTrade"; }

        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override { return "Open trade"; }
    };

    // Accepts the trade once the other player accepted it (the deprecated TradeStatusAction on TRADE_STATUS_TRADE_ACCEPT):
    // what the player put in for the bot, or asked from it, is traded. Cancels a trade with a player the bot does not
    // trade with.
    //   <Task class="Task_AcceptTrade"/>
    // Succeeded when the bot accepted (or there is nothing to accept), Failed when the trade was cancelled.
    class Task_AcceptTrade : public STAI_Task
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_Task> Create(const STAI_TaskConfig& config) { return std::make_unique<Task_AcceptTrade>(); }

        virtual const char* GetTaskClass() const override { return "Task_AcceptTrade"; }

        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override;
        virtual bool IsTickable() const override { return true; }

        virtual std::string GetDebugInfo() const override { return "Accept trade"; }
    };
}
