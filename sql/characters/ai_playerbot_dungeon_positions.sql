-- where a bot stands in a dungeon, its rtsc overrides (PlayerbotDungeonPositions): the cleared and the uncleared point
--   - of a stage of the map (InstanceSubsystem), preset NULL: set by rtsc while the master has no Bot Dungeon Planner
--     preset for the dungeon, and for the entrance (stage 0) on the bot's first visit; overrides the stage's positions
--   - of a pull of a preset (DungeonPlannerSubsystem, dungeon_planner_presets.id), stage = the pull's id (its lowest
--     creature db guid): set by rtsc while the pull is active; removed with the preset
CREATE TABLE IF NOT EXISTS `ai_playerbot_dungeon_positions` (
  `id` int(10) unsigned NOT NULL AUTO_INCREMENT,
  `guid` int(10) unsigned NOT NULL COMMENT 'bot character guid',
  `map` int(10) unsigned NOT NULL COMMENT 'dungeon map id',
  `preset` int(10) unsigned DEFAULT NULL COMMENT 'dungeon_planner_presets.id, NULL: the stages of the map',
  `stage` int(10) unsigned NOT NULL COMMENT 'stage index of the map, or the pull id of the preset',
  `cleared` tinyint(3) unsigned NOT NULL DEFAULT '1' COMMENT '1 the point once the stage / pull is cleared, 0 before',
  `position_x` float NOT NULL DEFAULT '0',
  `position_y` float NOT NULL DEFAULT '0',
  `position_z` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`id`),
  KEY `guid` (`guid`),
  KEY `preset` (`preset`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='PlayerbotAI dungeon positions';

-- the table before presets (primary key guid, map, stage, cleared): its rows were stage points; the overrides were
-- reset with this change (2026-10-25)
-- DROP TABLE IF EXISTS `ai_playerbot_dungeon_positions`;
