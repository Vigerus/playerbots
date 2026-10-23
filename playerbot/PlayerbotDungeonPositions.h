#pragma once

#include "Platform/Define.h"

#include <map>
#include <tuple>

class Player;

namespace ai
{
    // Where one bot stands in the stages (InstanceSubsystem) of dungeons, overriding the stage's registered
    // positions: a cleared and an uncleared point per stage, table ai_playerbot_dungeon_positions (characters
    // database), set by rtsc in a dungeon and for the entrance (stage 0) on the bot's first visit.
    // Loaded on first use, written on every change. Used from the bot's update (map thread).
    class PlayerbotDungeonPositions
    {
    public:
        struct Position
        {
            float x, y, z;
        };

        // the bot's cleared / uncleared point for the stage of the map, false when it has none
        bool Get(Player* bot, uint32 mapId, uint32 stage, bool cleared, Position& position);
        void Set(Player* bot, uint32 mapId, uint32 stage, bool cleared, const Position& position);
        void Remove(Player* bot, uint32 mapId, uint32 stage, bool cleared);

    private:
        void Load(Player* bot);

        bool loaded = false;
        std::map<std::tuple<uint32, uint32, bool>, Position> positions;     // (map, stage, cleared) -> position
    };
}
