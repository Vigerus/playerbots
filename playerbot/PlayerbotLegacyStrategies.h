#pragma once

#include "Platform/Define.h"

#include <map>
#include <set>
#include <string>
#include <utility>

class Player;

namespace ai
{
    // One bot's changes to the strategy lists of the StateNode_Legacy brains (Legacy_*.json): strategies added to
    // and removed from what the brain applies, per brain and map key (0 outdoors, else the map id of the dungeon /
    // raid / battleground), so the bot can be set up differently outdoors and per dungeon. Table
    // ai_playerbot_legacy_strategies (characters database), edited from the BotStateTree addon. Applied on top of
    // the brain whenever the brain is applied (StateNode_Legacy). Loaded on first use, written on every change.
    // Used from the bot's update (map thread).
    class PlayerbotLegacyStrategies
    {
    public:
        struct Changes
        {
            std::set<std::string> added;
            std::set<std::string> removed;

            bool empty() const { return added.empty() && removed.empty(); }
            // "-removed,...,+added,..."
            std::string Format() const;
        };

        // 0 outdoors (continents), else the map id of the instanceable map the bot is in
        static uint32 GetMapKey(Player* bot);

        // the bot's changes to the brain on the map key, nullptr when there are none
        const Changes* Get(Player* bot, const std::string& brain, uint32 mapKey);
        // adds ('+' strategy) or removes ('-' strategy) one strategy of the brain on the map key
        void Change(Player* bot, const std::string& brain, uint32 mapKey, char op, const std::string& strategy);
        // back to the brain's own list on the map key
        void Reset(Player* bot, const std::string& brain, uint32 mapKey);

    private:
        void Load(Player* bot);
        void Save(Player* bot, const std::string& brain, uint32 mapKey);

        bool loaded = false;
        std::map<std::pair<std::string, uint32>, Changes> changes;     // (brain, map key) -> changes
    };
}
