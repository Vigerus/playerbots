
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

void PlayerbotDbStore::Load(PlayerbotAI *ai)
{
    CCLOG_TRACE(m_logger) << "Source:[ENGINE] " << "Loading data for " << ai->GetBot()->GetName();

    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    auto results = CharacterDatabase.PQuery("SELECT `key`,`value` FROM `ai_playerbot_db_store` WHERE `guid` = '%lu'", guid);
    if (results)
    {
        ai->ClearStrategies(BotState::BOT_STATE_COMBAT);
        ai->ClearStrategies(BotState::BOT_STATE_NON_COMBAT);
        ai->ChangeStrategy("+chat", BotState::BOT_STATE_COMBAT);
        ai->ChangeStrategy("+chat", BotState::BOT_STATE_NON_COMBAT);

        std::list<std::string> values;
        do
        {
            Field* fields = results->Fetch();
            std::string key = fields[0].GetString();
            std::string value = fields[1].GetString();
            if (key == "value") values.push_back(value);
            else if (key == "co") ai->ChangeStrategy(value, BotState::BOT_STATE_COMBAT);
            else if (key == "nc") ai->ChangeStrategy(value, BotState::BOT_STATE_NON_COMBAT);
            else if (key == "dead") ai->ChangeStrategy(value, BotState::BOT_STATE_DEAD);
            else if (key == "react") ai->ChangeStrategy(value, BotState::BOT_STATE_REACTION);
        } while (results->NextRow());

        ai->GetAiObjectContext()->Load(values);
    }
}

void PlayerbotDbStore::Save(PlayerbotAI *ai)
{
    CCLOG_TRACE(m_logger) << "Source:[ENGINE] " << "Saving data for " << ai->GetBot()->GetName();

    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();

    Reset(ai);

    std::list<std::string> data = ai->GetAiObjectContext()->Save();
    for (std::list<std::string>::iterator i = data.begin(); i != data.end(); ++i)
    {
        SaveValue(guid, "value", *i);
    }

    SaveValue(guid, "co", FormatStrategies("co", ai->GetStrategies(BotState::BOT_STATE_COMBAT)));
    SaveValue(guid, "nc", FormatStrategies("nc", ai->GetStrategies(BotState::BOT_STATE_NON_COMBAT)));
    SaveValue(guid, "dead", FormatStrategies("dead", ai->GetStrategies(BotState::BOT_STATE_DEAD)));
    SaveValue(guid, "react", FormatStrategies("react", ai->GetStrategies(BotState::BOT_STATE_REACTION)));
}

std::string PlayerbotDbStore::FormatStrategies(std::string type, std::list<std::string_view> strategies)
{
    std::ostringstream out;
    for(const auto& strategy : strategies)
        out << "+" << strategy << ",";

    std::string res = out.str();
    return res.substr(0, res.size() - 1);
}

void PlayerbotDbStore::Reset(PlayerbotAI *ai)
{
    uint64 guid = ai->GetBot()->GetObjectGuid().GetRawValue();
    uint32 account = sObjectMgr.GetPlayerAccountIdByGUID(ObjectGuid(guid));

    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '%lu'", guid);
}

void PlayerbotDbStore::SaveValue(uint64 guid, std::string key, std::string value)
{
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `key`, `value`) VALUES ('%lu', '%s', '%s')", guid, key.c_str(), value.c_str());
}
