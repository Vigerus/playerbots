#include "PlayerbotTargetIcons.h"

#include "playerbot/ServerFacade.h"
#include "playerbot/StateTree/Providers/STAI_TargetIcons.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "Groups/Group.h"
#include "Maps/Map.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace ai;

namespace
{
    // the legacy value names, the start of their ai_playerbot_db_store rows ("rti>skull,cross")
    const std::string rtiPrefix = "rti>";
    const std::string rtiCcPrefix = "rti cc>";

    std::string Trim(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos)
            return "";
        return text.substr(first, text.find_last_not_of(" \t") - first + 1);
    }
}

std::vector<std::string> PlayerbotTargetIcons::Parse(const std::string& text, std::vector<std::string>* invalid)
{
    std::vector<std::string> icons;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        token = Trim(token);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (token.empty() || token == "none")
            continue;

        if (STAI_GetRtiIndex(token) < 0)
        {
            if (invalid)
                invalid->push_back(token);
            continue;
        }

        if (std::find(icons.begin(), icons.end(), token) == icons.end() && icons.size() < maxIcons)
            icons.push_back(token);
    }
    return icons;
}

std::string PlayerbotTargetIcons::Format(const std::vector<std::string>& icons)
{
    if (icons.empty())
        return "none";

    std::ostringstream out;
    for (size_t i = 0; i < icons.size(); ++i)
        out << (i ? "," : "") << icons[i];
    return out.str();
}

Unit* PlayerbotTargetIcons::GetMarkedUnit(Player* bot, const std::vector<std::string>& icons)
{
    Group* group = bot->GetGroup();
    if (!group || !bot->IsInWorld())
        return nullptr;

    for (const std::string& icon : icons)
    {
        const int index = STAI_GetRtiIndex(icon);
        if (index < 0)
            continue;

        const ObjectGuid guid = group->GetTargetIcon(index);
        if (guid.IsEmpty())
            continue;

        Unit* unit = bot->GetMap()->GetUnit(guid);
        if (unit && unit->IsInWorld() && sServerFacade.IsAlive(unit))
            return unit;
    }
    return nullptr;
}

void PlayerbotTargetIcons::SetRti(Player* bot, const std::vector<std::string>& icons)
{
    Load(bot);
    rti = icons;
    ++version;
    Save(bot);
}

void PlayerbotTargetIcons::SetRtiCc(Player* bot, const std::string& icon)
{
    Load(bot);
    rtiCc = icon;
    ++version;
    Save(bot);
}

void PlayerbotTargetIcons::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `value` FROM `ai_playerbot_db_store` WHERE `guid` = '" UI64FMTD "' AND `preset` = '' AND `key` = 'value'",
        bot->GetObjectGuid().GetRawValue());
    if (!result)
        return;

    do
    {
        const std::string value = result->Fetch()[0].GetCppString();
        const bool cc = value.rfind(rtiCcPrefix, 0) == 0;
        if (!cc && value.rfind(rtiPrefix, 0) != 0)
            continue;

        // "none" is a saved empty list; a row without a known icon keeps the default
        const std::string text = value.substr((cc ? rtiCcPrefix : rtiPrefix).size());
        const std::vector<std::string> icons = Parse(text);
        if (icons.empty() && text != "none")
            continue;
        if (!cc)
            rti = icons;
        else
            // the old engine kept a list: its first icon
            rtiCc = icons.empty() ? "" : icons.front();
    }
    while (result->NextRow());
}

void PlayerbotTargetIcons::Save(Player* bot)
{
    const uint64 guid = bot->GetObjectGuid().GetRawValue();
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '" UI64FMTD "' AND `preset` = '' AND `key` = 'value' AND (`value` LIKE 'rti>%%' OR `value` LIKE 'rti cc>%%')",
        guid);
    // icon names only (Parse): nothing to escape
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES ('" UI64FMTD "', '', 'value', '%s'), ('" UI64FMTD "', '', 'value', '%s')",
        guid, (rtiPrefix + Format(rti)).c_str(), guid, (rtiCcPrefix + (rtiCc.empty() ? "none" : rtiCc)).c_str());
}
