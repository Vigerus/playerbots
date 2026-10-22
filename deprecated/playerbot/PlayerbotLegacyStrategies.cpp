#include "PlayerbotLegacyStrategies.h"

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
    auto itr = changes.find({ brain, mapKey });
    return itr != changes.end() && !itr->second.empty() ? &itr->second : nullptr;
}

void PlayerbotLegacyStrategies::Change(Player* bot, const std::string& brain, uint32 mapKey, char op, const std::string& strategy)
{
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
}

void PlayerbotLegacyStrategies::Reset(Player* bot, const std::string& brain, uint32 mapKey)
{
    changes.erase({ brain, mapKey });
}
