#pragma once

#include "Platform/Define.h"

#include <map>
#include <tuple>

class Player;

namespace ai
{
    // Where one bot stands in dungeons, its rtsc overrides, table ai_playerbot_dungeon_positions (characters
    // database): a cleared and an uncleared point per
    //   - stage of the map (InstanceSubsystem), preset NULL (0 here): set by rtsc while the master has no preset for
    //     the dungeon, and for the entrance (stage 0) on the bot's first visit; overrides the stage's positions
    //   - pull of a Bot Dungeon Planner preset (DungeonPlannerSubsystem), preset = its id, stage = the pull's id (its
    //     lowest creature db guid): set by rtsc while the pull is active; overrides the preset's positions. Removed
    //     with the preset.
    // Loaded on first use, written on every change. Used from the bot's update (map thread).
    class PlayerbotDungeonPositions
    {
    public:
        struct Position
        {
            float x, y, z;
        };

        // the bot's cleared / uncleared point for the stage (preset 0) or the preset's pull, false when it has none
        bool Get(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared, Position& position);
        void Set(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared, const Position& position);
        void Remove(Player* bot, uint32 mapId, uint32 preset, uint32 stage, bool cleared);

    private:
        void Load(Player* bot);

        bool loaded = false;
        std::map<std::tuple<uint32, uint32, uint32, bool>, Position> positions;   // (map, preset, stage, cleared) -> position
    };
}
