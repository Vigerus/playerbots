
#include "playerbot/playerbot.h"
#include "Action.h"
#include "Queue.h"

#include "playerbot/PlayerbotAIConfig.h"
using namespace ai;


void Queue::Push(ActionBasket *action)
{
    if (action && action->getAction())
    {
        const std::string& search_name = action->getAction()->getName();
        auto it = find_if(actions.begin(), actions.end(), [search_name](ActionBasket* el){ return el->getAction()->getName() == search_name;});

        if (it != actions.end())
        {
            ActionBasket* basket = *it;
                if (basket->getRelevance() < action->getRelevance())
                {
                    basket->setRelevance(action->getRelevance());
                    basket->setEvent(action->getEvent());
                }
            ActionNode* actionNode = action->getAction();
            if (actionNode)
                delete actionNode;
            delete action;
            return;
        }

        actions.push_back(action);
    }
}

ActionNode* Queue::Pop(ActionBasket* selection)
{
    if (selection == nullptr)
    {
        selection = Peek();
    }

    if (selection != nullptr)
    {
        ActionNode* action = selection->getAction();
        actions.remove(selection);
        delete selection;
        return action;
    }

    return nullptr;
}

ActionBasket* Queue::Peek()
{
    float max_relevance = -400;
    ActionBasket* selection = nullptr;

    for (ActionBasket* basket : actions)
    {
        const float relevance = basket->getRelevance();

        if (relevance > max_relevance)
        {
            max_relevance = basket->getRelevance();
            selection = basket;
        }
    }
    return selection;
}

int Queue::Size()
{
    return actions.size();
}

void Queue::RemoveExpired()
{
    std::list<ActionBasket*> expired;
    for (ActionBasket* basket : actions)
    {
        if (sPlayerbotAIConfig.expireActionTime && basket->isExpired(sPlayerbotAIConfig.expireActionTime / 1000))
        {
            expired.push_back(basket);
        }
    }

    for (ActionBasket* basket : expired)
    {
        actions.remove(basket);
        if (ActionNode* action = basket->getAction())
        {
            sLog.outDebug("Action %s is expired", action->getName().c_str());
            delete action;
        }
        delete basket;
    }
}
