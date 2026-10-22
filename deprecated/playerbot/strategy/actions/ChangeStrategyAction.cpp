
#include "playerbot/playerbot.h"
#include "ChangeStrategyAction.h"
#include "playerbot/PlayerbotAIConfig.h"

using namespace ai;

// strategy changes are runtime only: the bot's strategies come from the state tree / legacy brains (json)

bool ChangeCombatStrategyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_COMBAT);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_COMBAT);
    }
    
    return true;
}

bool ChangeNonCombatStrategyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_NON_COMBAT);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_NON_COMBAT);
    }

    return true;
}

bool ChangeDeadStrategyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_DEAD);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_DEAD);
    }

    return true;
}

bool ChangeReactionStrategyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    text = text.empty() ? getName() : text;

    ai->ChangeStrategy(text, BotState::BOT_STATE_REACTION);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_REACTION);
    }

    return true;
}

bool ChangeAllStrategyAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    std::string strategyName = text.empty() ? strategy : text;

    ai->ChangeStrategy(strategyName, BotState::BOT_STATE_ALL);

    if (text.find("?") != std::string::npos)
    {
        ai->PrintStrategies(requester, BotState::BOT_STATE_ALL);
    }

    return true;
}