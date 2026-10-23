-- the totems a shaman bot keeps up, one per school, each the first rank of a totem spell or 0 for none
-- (PlayerbotTotems, TotemProvider_GlobalTask): the outdoor set is map 0 stage 0, the others are the sets of dungeon
-- stages (InstanceSubsystem), used while the master stands in the stage; also created by the server on first use
CREATE TABLE IF NOT EXISTS `ai_playerbot_totems` (
  `guid` int(10) unsigned NOT NULL COMMENT 'bot character guid',
  `map` int(10) unsigned NOT NULL COMMENT 'dungeon map id, 0 the outdoor set',
  `stage` int(10) unsigned NOT NULL COMMENT 'stage index of the map (InstanceSubsystem::RegisterStages), 0 the outdoor set',
  `fire` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'fire totem spell (first rank), 0 none',
  `earth` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'earth totem spell (first rank), 0 none',
  `water` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'water totem spell (first rank), 0 none',
  `air` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'air totem spell (first rank), 0 none',
  PRIMARY KEY (`guid`, `map`, `stage`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='PlayerbotAI totem selections';
