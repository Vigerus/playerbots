-- a bot's changes to the strategy lists of the StateTree legacy brains (Legacy_*.json), set from the BotStateTree
-- addon; applied on top of the brain (PlayerbotLegacyStrategies)
CREATE TABLE IF NOT EXISTS `ai_playerbot_legacy_strategies` (
  `guid` int(10) unsigned NOT NULL COMMENT 'bot character guid',
  `brain` varchar(64) NOT NULL COMMENT 'brain name of the Legacy_*.json strategy list',
  `map` int(10) unsigned NOT NULL COMMENT '0 outdoors, else the dungeon / raid / battleground map id',
  `strategies` varchar(4000) NOT NULL DEFAULT '' COMMENT '-removed,...,+added,...',
  PRIMARY KEY (`guid`, `brain`, `map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 ROW_FORMAT=DYNAMIC COMMENT='PlayerbotAI legacy brain strategy changes';
