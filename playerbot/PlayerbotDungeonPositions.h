#pragma once

#include "Platform/Define.h"

#include <map>
#include <utility>

class Player;

namespace ai
{
    // Where one bot stands in the stages (InstanceSubsystem) of dungeons, overriding the stage's registered
    // positions: table ai_playerbot_dungeon_positions (characters database), set by rtsc in a dungeon.
    // Loaded on first use, written on every change. Used from the bot's update (map thread).
    class PlayerbotDungeonPositions
    {
    public:
        struct Position
        {
            float x, y, z;
        };

        // the bot's position for the stage of the map, false when it has none
        bool Get(Player* bot, uint32 mapId, uint32 stage, Position& position);
        void Set(Player* bot, uint32 mapId, uint32 stage, const Position& position);
        void Remove(Player* bot, uint32 mapId, uint32 stage);

    private:
        void Load(Player* bot);

        bool loaded = false;
        std::map<std::pair<uint32, uint32>, Position> positions;     // (map, stage) -> position
    };
}
