#include "PlayerbotLegacyStrategies.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "Maps/Map.h"

#include <sstream>

using namespace ai;

std::string PlayerbotLegacyStrategies::Changes::Format() const
{
    std::ostringstream out;
    bool first = true;
    for (const std::string& strategy : removed)
    {
        out << (first ? "" : ",") << "-" << strategy;
        first = false;
    }
    for (const std::string& strategy : added)
    {
        out << (first ? "" : ",") << "+" << strategy;
        first = false;
    }
    return out.str();
}

uint32 PlayerbotLegacyStrategies::GetMapKey(Player* bot)
{
    if (!bot->IsInWorld())
        return 0;

    Map* map = bot->GetMap();
    return map && map->Instanceable() ? map->GetId() : 0;
}

const PlayerbotLegacyStrategies::Changes* PlayerbotLegacyStrategies::Get(Player* bot, const std::string& brain, uint32 mapKey)
{
    Load(bot);
    auto itr = changes.find({ brain, mapKey });
    return itr != changes.end() && !itr->second.empty() ? &itr->second : nullptr;
}

void PlayerbotLegacyStrategies::Change(Player* bot, const std::string& brain, uint32 mapKey, char op, const std::string& strategy)
{
    Load(bot);
    Changes& entry = changes[{ brain, mapKey }];
    if (op == '+')
    {
        entry.removed.erase(strategy);
        entry.added.insert(strategy);
    }
    else
    {
        entry.added.erase(strategy);
        entry.removed.insert(strategy);
    }
    Save(bot, brain, mapKey);
}

void PlayerbotLegacyStrategies::Reset(Player* bot, const std::string& brain, uint32 mapKey)
{
    Load(bot);
    changes.erase({ brain, mapKey });
    Save(bot, brain, mapKey);
}

void PlayerbotLegacyStrategies::Save(Player* bot, const std::string& brain, uint32 mapKey)
{
    std::string brainName = brain;
    CharacterDatabase.escape_string(brainName);

    auto itr = changes.find({ brain, mapKey });
    if (itr == changes.end() || itr->second.empty())
    {
        CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_legacy_strategies` WHERE `guid` = '%u' AND `brain` = '%s' AND `map` = '%u'",
            bot->GetGUIDLow(), brainName.c_str(), mapKey);
        return;
    }

    std::string strategies = itr->second.Format();
    CharacterDatabase.escape_string(strategies);
    CharacterDatabase.PExecute("REPLACE INTO `ai_playerbot_legacy_strategies` (`guid`, `brain`, `map`, `strategies`) VALUES ('%u', '%s', '%u', '%s')",
        bot->GetGUIDLow(), brainName.c_str(), mapKey, strategies.c_str());
}

void PlayerbotLegacyStrategies::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `brain`, `map`, `strategies` FROM `ai_playerbot_legacy_strategies` WHERE `guid` = '%u'",
        bot->GetGUIDLow());
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        Changes& entry = changes[{ fields[0].GetCppString(), fields[1].GetUInt32() }];

        std::istringstream list(fields[2].GetCppString());
        std::string item;
        while (std::getline(list, item, ','))
        {
            if (item.size() < 2)
                continue;
            if (item[0] == '+')
                entry.added.insert(item.substr(1));
            else if (item[0] == '-')
                entry.removed.insert(item.substr(1));
        }
    }
    while (result->NextRow());
}
