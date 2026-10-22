#ifndef _PlayerbotDbStore_H
#define _PlayerbotDbStore_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "PlayerbotMgr.h"

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>

// the bot's values (ai_playerbot_db_store). Strategies are not stored: they come from the state tree / legacy
// brains (json), "co" / "nc" commands and the BotStateTree addon change them at runtime only
class PlayerbotDbStore
{
public:
    PlayerbotDbStore();
    virtual ~PlayerbotDbStore();
    static PlayerbotDbStore& instance()
    {
        static PlayerbotDbStore instance;
        return instance;
    }

    void Save(PlayerbotAI *ai, std::string preset = "");
    void Load(PlayerbotAI *ai, std::string preset = "");
    void Reset(PlayerbotAI *ai, std::string preset = "");

private:
    log4cxx::LoggerPtr m_logger;

    void SaveValue(uint64 guid, std::string preset, std::string key, std::string value);
};

#define sPlayerbotDbStore PlayerbotDbStore::instance()

#endif
