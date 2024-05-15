CREATE TABLE IF NOT EXISTS ai_playerbot_guildbuffs (
`guildid` smallint(20) NOT NULL,
`isbuff` tinyint(1) unsigned NOT NULL DEFAULT '0',
`buffid` bigint(6) NOT NULL
PRIMARY KEY(`guildid`)
);

INSERT INTO ai_playerbot_guildbuffs (`guildid`, `isbuff`, `buffid`) VALUES
	(1, 1, 16609),
	(1, 1, 24382),
	(1, 0, 13445),
	(1, 0, 13452),
	(1, 0, 13454),
	(1, 0, 13455),
	(1, 0, 13457),
	(1, 0, 20076),
	(1, 0, 20077),
	(1, 0, 20078),
	(1, 0, 20749)
;