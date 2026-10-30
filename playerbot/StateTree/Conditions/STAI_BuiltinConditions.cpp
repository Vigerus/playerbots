#include "STAI_BuiltinConditions.h"

#include "playerbot/StateTree/STAI_TreeInstance.h"
#include "Util/Util.h"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    std::string Attribute(const STAI_TaskConfig& config, const std::string& name)
    {
        return config.get<std::string>("<xmlattr>." + name, "");
    }

    float ParseChance(const STAI_TaskConfig& config)
    {
        const std::string value = Attribute(config, "chance");
        char* end = nullptr;
        const double chance = std::strtod(value.c_str(), &end);
        if (value.empty() || *end || chance < 0.0 || chance > 100.0)
            throw std::runtime_error("chance=\"...\" needs a percentage from 0 to 100, got '" + value + "'");
        return float(chance);
    }

    uint8 ParseLocation(const STAI_TaskConfig& config)
    {
        const std::string value = Attribute(config, "location");
        if (value.empty())
            return Condition_ActiveStateLocation::Above | Condition_ActiveStateLocation::Below;

        uint8 mask = 0;
        std::stringstream list(value);
        std::string item;
        while (std::getline(list, item, ','))
        {
            item.erase(0, item.find_first_not_of(' '));
            item.erase(item.find_last_not_of(' ') + 1);
            if (item == "above")
                mask |= Condition_ActiveStateLocation::Above;
            else if (item == "this")
                mask |= Condition_ActiveStateLocation::This;
            else if (item == "below")
                mask |= Condition_ActiveStateLocation::Below;
            else
                throw std::runtime_error("location=\"...\" takes above, this and below (comma separated), got '" + item + "'");
        }
        return mask;
    }

    uint32 ParseMs(const STAI_TaskConfig& config)
    {
        const std::string value = Attribute(config, "ms");
        char* end = nullptr;
        const long ms = std::strtol(value.c_str(), &end, 10);
        if (value.empty() || *end || ms < 0)
            throw std::runtime_error("ms=\"...\" needs milliseconds, got '" + value + "'");
        return uint32(ms);
    }
}

void Condition_RandomChance::Validate(const STAI_TaskConfig& config)
{
    ParseChance(config);
}

std::unique_ptr<STAI_ConditionBase> Condition_RandomChance::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Condition_RandomChance>(ParseChance(config));
}

bool Condition_RandomChance::OnTestCondition(STAI_NodeContext& context) const
{
    return roll_chance_f(chance);
}

std::string Condition_RandomChance::GetDebugInfo() const
{
    std::ostringstream out;
    out << "chance " << chance << "%";
    return out.str();
}

void Condition_ActiveStateLocation::Validate(const STAI_TaskConfig& config)
{
    ParseLocation(config);
}

std::unique_ptr<STAI_ConditionBase> Condition_ActiveStateLocation::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Condition_ActiveStateLocation>(ParseLocation(config));
}

bool Condition_ActiveStateLocation::OnTestCondition(STAI_NodeContext& context) const
{
    const int owner = context.ownerState;
    const int leaf = context.instance ? context.instance->GetActiveLeaf() : -1;
    if (owner < 0 || leaf < 0)
        return true;

    const STAI_StateDef& state = context.instance->GetAsset().states[owner];
    return ((mask & Above) && leaf < owner)
        || ((mask & This) && context.instance->IsStateActive(owner))
        || ((mask & Below) && leaf >= state.subtreeEnd);
}

std::string Condition_ActiveStateLocation::GetDebugInfo() const
{
    if (mask == (Above | Below))
        return "don't reenter";

    std::string info = "active state is";
    const char* separator = " ";
    if (mask & Above)
    {
        info += separator + std::string("above");
        separator = " or ";
    }
    if (mask & Below)
    {
        info += separator + std::string("below");
        separator = " or ";
    }
    if (mask & This)
        info += separator + std::string("this state");
    return info;
}

void Condition_TimeInState::Validate(const STAI_TaskConfig& config)
{
    ParseMs(config);
}

std::unique_ptr<STAI_ConditionBase> Condition_TimeInState::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Condition_TimeInState>(ParseMs(config));
}

bool Condition_TimeInState::OnTestCondition(STAI_NodeContext& context) const
{
    if (!context.instance || context.ownerState < 0 || !context.instance->IsStateActive(context.ownerState))
        return false;
    return context.instance->GetTimeInState(context.ownerState, WorldTimer::getMSTime()) >= ms;
}
