#pragma once

#include "PlayerbotAI.h"

#include <map>

class Player;

// talent spec and group roles of a player (was part of the legacy AiFactory, see deprecated/)
class PlayerSpec
{
public:
    // the talent tab with the most points (a class default below level 10)
    static int GetPlayerSpecTab(const Player* player);
    // talent points per tab
    static std::map<uint32, int32> GetPlayerSpecTabs(const Player* player);
    static BotRoles GetPlayerRoles(const Player* player);
};
