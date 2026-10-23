#include "PlayerbotDungeonPositions.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"

using namespace ai;

bool PlayerbotDungeonPositions::Get(Player* bot, uint32 mapId, uint32 stage, bool cleared, Position& position)
{
    Load(bot);
    auto itr = positions.find({ mapId, stage, cleared });
    if (itr == positions.end())
        return false;

    position = itr->second;
    return true;
}

void PlayerbotDungeonPositions::Set(Player* bot, uint32 mapId, uint32 stage, bool cleared, const Position& position)
{
    Load(bot);
    positions[{ mapId, stage, cleared }] = position;
    CharacterDatabase.PExecute("REPLACE INTO `ai_playerbot_dungeon_positions` (`guid`, `map`, `stage`, `cleared`, `position_x`, `position_y`, `position_z`) VALUES ('%u', '%u', '%u', '%u', '%f', '%f', '%f')",
        bot->GetGUIDLow(), mapId, stage, cleared ? 1 : 0, position.x, position.y, position.z);
}

void PlayerbotDungeonPositions::Remove(Player* bot, uint32 mapId, uint32 stage, bool cleared)
{
    Load(bot);
    positions.erase({ mapId, stage, cleared });
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u' AND `map` = '%u' AND `stage` = '%u' AND `cleared` = '%u'",
        bot->GetGUIDLow(), mapId, stage, cleared ? 1 : 0);
}

void PlayerbotDungeonPositions::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `map`, `stage`, `cleared`, `position_x`, `position_y`, `position_z` FROM `ai_playerbot_dungeon_positions` WHERE `guid` = '%u'",
        bot->GetGUIDLow());
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        positions[{ fields[0].GetUInt32(), fields[1].GetUInt32(), fields[2].GetUInt8() != 0 }] = { fields[3].GetFloat(), fields[4].GetFloat(), fields[5].GetFloat() };
    }
    while (result->NextRow());
}
