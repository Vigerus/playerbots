
#include "playerbot/playerbot.h"
#include "playerbot/Helpers.h"
#include "RtiValue.h"

using namespace ai;

RtiValue::RtiValue(PlayerbotAI* ai)
    : ManualSetValue<std::vector<std::string>>(ai, {"skull"}, "rti")
{
}

std::string RtiValue::Save()
{
    std::string s;

    for (uint32 i = 0; i < value.size(); ++i)
    {
        if (i) { s+=","; }
        s+=value[i];
    }

    return s;
}

bool RtiValue::Load(std::string text)
{
    std::stringstream ss(text);
    std::string token;
    std::vector<std::string> rti_list;

    while (std::getline(ss, token, ','))
    {
        trim(token);
        value.push_back(token);
    }

    if (!rti_list.empty())
    {
        value = rti_list;
        return true;
    }

    return false;
}

RtiCcValue::RtiCcValue(PlayerbotAI* ai)
    : ManualSetValue<std::vector<std::string>>(ai, {"moon"}, "rti cc")
{
}

std::string RtiCcValue::Save()
{
    std::string s;

    for (uint32 i = 0; i < value.size(); ++i)
    {
        if (i) { s += ","; }
        s += value[i];
    }

    return s;
}

bool RtiCcValue::Load(std::string text)
{
    std::stringstream ss(text);
    std::string token;
    std::vector<std::string> rti_list;

    while (std::getline(ss, token, ','))
    {
        trim(token);
        value.push_back(token);
    }

    if (!rti_list.empty())
    {
        value = rti_list;
        return true;
    }

    return false;
}
