-- where a bot stands in a dungeon stage (InstanceSubsystem), set by rtsc in the dungeon; overrides the stage's
-- registered positions (PlayerbotDungeonPositions)
CREATE TABLE IF NOT EXISTS `ai_playerbot_dungeon_positions` (
  `guid` int(10) unsigned NOT NULL COMMENT 'bot character guid',
  `map` int(10) unsigned NOT NULL COMMENT 'dungeon map id',
  `stage` int(10) unsigned NOT NULL COMMENT 'stage index of the map (InstanceSubsystem::RegisterStages)',
  `position_x` float NOT NULL DEFAULT '0',
  `position_y` float NOT NULL DEFAULT '0',
  `position_z` float NOT NULL DEFAULT '0',
  PRIMARY KEY (`guid`, `map`, `stage`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='PlayerbotAI dungeon stage positions';
