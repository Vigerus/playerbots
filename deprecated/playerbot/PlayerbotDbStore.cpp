
#include "playerbot/playerbot.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/PlayerbotFactory.h"
#include "PlayerbotDbStore.h"
#include <cstdlib>
#include <iostream>
#include "Log/LogHelper.h"

#include "LootObjectStack.h"
#include "strategy/values/Formations.h"
#include "strategy/values/PositionValue.h"
INSTANTIATE_SINGLETON_1(PlayerbotDbStore);

using namespace ai;

PlayerbotDbStore::PlayerbotDbStore() :
    m_logger(log4cxx::Logger::getLogger("PlayerbotDbStore"))
{
}

PlayerbotDbStore::~PlayerbotDbStore() {}

void PlayerbotDbStore::Load(PlayerbotAI *ai, std::string preset)
{
    CCLOG_TRACE(m_logger) << "Source:[ENGINE] " << "Loading data for " << ai->GetBot()->GetName();

    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    auto results = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
    if (results)
    {
        // strategies are not stored: they come from the state tree / legacy brains (json) only, "co"/"nc"/"dead"
        // rows of older versions are ignored
        std::list<std::string> values;
        do
        {
            Field* fields = results->Fetch();
            std::string key = fields[0].GetString();
            std::string value = fields[1].GetString();
            if (key == "value") values.push_back(value);
        } while (results->NextRow());

        ai->GetAiObjectContext()->Load(values);
    }
}

void PlayerbotDbStore::Save(PlayerbotAI *ai, std::string preset)
{
    CCLOG_TRACE(m_logger) << "Source:[ENGINE] " << "Saving data for " << ai->GetBot()->GetName() << " in a <" << (preset.empty() ? "default" : preset.c_str()) << "> preset";

    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    Reset(ai, preset);

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    for (std::list<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(guid, preset, "value", *i);
    }
}

void PlayerbotDbStore::Reset(PlayerbotAI *ai, std::string preset)
{
    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();
    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(guid));

    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%lu' AND `preset` = '%s'", guid, preset.c_str());
}

void PlayerbotDbStore::SaveValue(uint64 guid, std::string preset, std::string key, std::string value)
{
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES ('%lu', '%s', '%s', '%s')", guid, preset.c_str(), key.c_str(), value.c_str());
}
