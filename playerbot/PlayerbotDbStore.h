#ifndef _PlayerbotDbStore_H
#define _PlayerbotDbStore_H

#include "Common.h"
#include "PlayerbotAIBase.h"
#include "PlayerbotMgr.h"

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>

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

    void Save(PlayerbotAI *ai);
    void Load(PlayerbotAI *ai);
    void Reset(PlayerbotAI *ai);

private:
    log4cxx::LoggerPtr m_logger;

    void SaveValue(uint64 guid, std::string key, std::string value);
    std::string FormatStrategies(std::string type, std::list<std::string_view> strategies);
};

#define sPlayerbotDbStore PlayerbotDbStore::instance()

#endif
