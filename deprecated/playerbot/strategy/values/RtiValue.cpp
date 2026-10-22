#include "playerbot/playerbot.h"
#include "playerbot/Helpers.h"
#include "RtiValue.h"
#include "RtiTargetValue.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace ai;

std::vector<std::string> ai::ParseRtiList(const std::string& text, std::vector<std::string>* invalid)
{
    std::vector<std::string> rtiList;

    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        trim(token);
        std::transform(token.begin(), token.end(), token.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (token.empty() || token == "none")
            continue;

        if (RtiTargetValue::GetRtiIndex(token) < 0)
        {
            if (invalid)
                invalid->push_back(token);
            continue;
        }

        if (std::find(rtiList.begin(), rtiList.end(), token) == rtiList.end() && rtiList.size() < MAX_RTI_ICONS)
            rtiList.push_back(token);
    }

    return rtiList;
}

std::string ai::FormatRtiList(const std::vector<std::string>& rtiList)
{
    if (rtiList.empty())
        return "none";

    std::string text;
    for (const std::string& rti : rtiList)
    {
        if (!text.empty())
            text += ",";
        text += rti;
    }
    return text;
}

RtiValue::RtiValue(PlayerbotAI* ai)
    : ManualSetValue<std::vector<std::string>>(ai, {"skull"}, "rti")
{
}

std::string RtiValue::Save()
{
    return FormatRtiList(value);
}

bool RtiValue::Load(std::string text)
{
    // "none" (saved empty list) loads as empty, anything without a known icon keeps the default
    std::vector<std::string> rtiList = ParseRtiList(text);
    if (rtiList.empty() && text != "none")
        return false;

    value = rtiList;
    return true;
}

RtiCcValue::RtiCcValue(PlayerbotAI* ai)
    : ManualSetValue<std::vector<std::string>>(ai, {"moon"}, "rti cc")
{
}

std::string RtiCcValue::Save()
{
    return FormatRtiList(value);
}

bool RtiCcValue::Load(std::string text)
{
    std::vector<std::string> rtiList = ParseRtiList(text);
    if (rtiList.empty() && text != "none")
        return false;

    value = rtiList;
    return true;
}
