#include "PlayerbotDungeonPositions.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"

#include <string>

using namespace ai;

namespace
{
    // the preset column: NULL for the stages of the map
    std::string PresetValue(uint32 preset)
    {
        return preset ? "'" + std::to_string(preset) + "'" : "NULL";
    }

    std::string PresetCondition(uint32 preset)
    {
        return preset ? "`preset` = '" + std::to_string(preset) + "'" : "`preset` IS NULL";
    }
}

bool PlayerbotDungeonPositions::Get(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared, Position& position)
{
    Load(bot);
    auto itr = positions.find({ mapId, preset, stage, cleared });
    if (itr == positions.end())
        return false;

    position = itr->second;
    return true;
}

void PlayerbotDungeonPositions::Set(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared, const Position& position)
{
    Load(bot);
    positions[{ mapId, preset, stage, cleared }] = position;
    // the preset can be NULL (no unique key over it): the old row goes first
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u' AND `map` = '%u' AND %s AND `stage` = '%u' AND `cleared` = '%u'",
        bot->GetGUIDLow(), mapId, PresetCondition(preset).c_str(), stage, cleared ? 1 : 0);
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_dungeon_positions` (`guid`, `map`, `preset`, `stage`, `cleared`, `position_x`, `position_y`, `position_z`) VALUES ('%u', '%u', %s, '%u', '%u', '%f', '%f', '%f')",
        bot->GetGUIDLow(), mapId, PresetValue(preset).c_str(), stage, cleared ? 1 : 0, position.x, position.y, position.z);
}

void PlayerbotDungeonPositions::Remove(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared)
{
    Load(bot);
    positions.erase({ mapId, preset, stage, cleared });
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u' AND `map` = '%u' AND %s AND `stage` = '%u' AND `cleared` = '%u'",
        bot->GetGUIDLow(), mapId, PresetCondition(preset).c_str(), stage, cleared ? 1 : 0);
}

void PlayerbotDungeonPositions::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `map`, `preset`, `stage`, `cleared`, `position_x`, `position_y`, `position_z` FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u'",
        bot->GetGUIDLow());
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        // a NULL preset reads as 0
        positions[{ fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetUInt32(), fields[3].GetUInt8() != 0 }] = { fields[4].GetFloat(), fields[5].GetFloat(), fields[6].GetFloat() };
    }
    while (result->NextRow());
}
