-- where a bot stands in a dungeon stage (InstanceSubsystem) while its master is in it: the cleared and the uncleared
-- point, set by rtsc in the dungeon and for the entrance (stage 0) on the bot's first visit; overrides the stage's
-- registered positions (PlayerbotDungeonPositions)
CREATE TABLE IF NOT EXISTS `ai_playerbot_dungeon_positions` (
  `guid` int(10) unsigned NOT NULL COMMENT 'bot character guid',
  `map` int(10) unsigned NOT NULL COMMENT 'dungeon map id',
  `stage` int(10) unsigned NOT NULL COMMENT 'stage index of the map (InstanceSubsystem::RegisterStages)',
  `cleared` tinyint(3) unsigned NOT NULL DEFAULT '1' COMMENT '1 the point once the stage is cleared, 0 before',
  `position_x` float NOT NULL DEFAULT '0',
  `position_y` float NOT NULL DEFAULT '0',
  `position_z` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `map`, `stage`, `cleared`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='PlayerbotAI dungeon stage positions';

-- a table made before the cleared / uncleared points: its rows are cleared points
-- ALTER TABLE `ai_playerbot_dungeon_positions`
--   ADD COLUMN `cleared` tinyint(3) unsigned NOT NULL DEFAULT '1' COMMENT '1 the point once the stage is cleared, 0 before' AFTER `stage`,
--   DROP PRIMARY KEY, ADD PRIMARY KEY (`guid`, `map`, `stage`, `cleared`);
