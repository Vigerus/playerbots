#include "PlayerbotDungeonPositions.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"

using namespace ai;

bool PlayerbotDungeonPositions::Get(Player* bot, uint32 mapId, uint32 stage, Position& position)
{
    Load(bot);
    auto itr = positions.find({ mapId, stage });
    if (itr == positions.end())
        return false;

    position = itr->second;
    return true;
}

void PlayerbotDungeonPositions::Set(Player* bot, uint32 mapId, uint32 stage, const Position& position)
{
    Load(bot);
    positions[{ mapId, stage }] = position;
    CharacterDatabase.PExecute("REPLACE INTO `ai_playerbot_dungeon_positions` (`guid`, `map`, `stage`, `position_x`, `position_y`, `position_z`) VALUES ('%u', '%u', '%u', '%f', '%f', '%f')",
        bot->GetGUIDLow(), mapId, stage, position.x, position.y, position.z);
}

void PlayerbotDungeonPositions::Remove(Player* bot, uint32 mapId, uint32 stage)
{
    Load(bot);
    positions.erase({ mapId, stage });
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u' AND `map` = '%u' AND `stage` = '%u'",
        bot->GetGUIDLow(), mapId, stage);
}

void PlayerbotDungeonPositions::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `map`, `stage`, `position_x`, `position_y`, `position_z` FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u'",
        bot->GetGUIDLow());
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        positions[{ fields[0].GetUInt32(), fields[1].GetUInt32() }] = { fields[2].GetFloat(), fields[3].GetFloat(), fields[4].GetFloat() };
    }
    while (result->NextRow());
}
