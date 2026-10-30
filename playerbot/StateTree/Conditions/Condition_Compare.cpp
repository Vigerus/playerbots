#include "Condition_Compare.h"

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

    struct Parsed
    {
        std::string binding;
        Condition_Compare::Compare compare = Condition_Compare::Compare::None;
        std::string compareText;
        double value = 0.0;
        bool hasKey = false;
        double key = 0.0;
        bool isText = false;
        std::string text;
    };

    Parsed ParseCompare(const STAI_TaskConfig& config)
    {
        using Compare = Condition_Compare::Compare;

        Parsed parsed;
        parsed.binding = Attribute(config, "binding");
        if (parsed.binding.empty())
            throw std::runtime_error("<Condition class=\"Condition_Compare\"> needs a binding=\"Root|...\" (a task property)");

        const std::string key = Attribute(config, "key");
        if (!key.empty())
        {
            char* end = nullptr;
            parsed.key = std::strtod(key.c_str(), &end);
            if (*end)
                throw std::runtime_error("key needs a number, got '" + key + "'");
            parsed.hasKey = true;
        }

        parsed.compareText = Attribute(config, "compare");
        if (parsed.compareText.empty())
            return parsed;

        static const std::pair<const char*, Compare> compares[] =
        {
            { "equal", Compare::Equal }, { "lesser", Compare::Lesser }, { "lesser or equal", Compare::LesserOrEqual },
            { "greater", Compare::Greater }, { "greater or equal", Compare::GreaterOrEqual },
        };
        for (const auto& [name, compare] : compares)
            if (parsed.compareText == name)
                parsed.compare = compare;
        if (parsed.compare == Compare::None)
            throw std::runtime_error("compare must be equal, lesser, lesser or equal, greater or greater or equal, got '" + parsed.compareText + "'");

        const std::string value = Attribute(config, "value");
        char* end = nullptr;
        parsed.value = std::strtod(value.c_str(), &end);
        if (value.empty() || *end)
        {
            // a text: only compared for equality
            if (value.empty() || parsed.compare != Compare::Equal)
                throw std::runtime_error("compare needs a number in value=\"...\" (a text only with compare=\"equal\"), got '" + value + "'");
            parsed.isText = true;
            parsed.text = value;
        }
        return parsed;
    }
}

void Condition_Compare::Validate(const STAI_TaskConfig& config)
{
    ParseCompare(config);
}

std::unique_ptr<STAI_ConditionBase> Condition_Compare::Create(const STAI_TaskConfig& config)
{
    const Parsed parsed = ParseCompare(config);

    auto condition = std::make_unique<Condition_Compare>();
    condition->binding = parsed.binding;
    condition->compare = parsed.compare;
    condition->compareText = parsed.compareText;
    condition->value = parsed.value;
    condition->hasKey = parsed.hasKey;
    condition->key = parsed.key;
    condition->isText = parsed.isText;
    condition->text = parsed.text;
    condition->AddPropertyRef(parsed.binding);
    return condition;
}

void Condition_Compare::ValidatePropertyRef(size_t index, const STAI_PropertyDef& property) const
{
    if (hasKey)
    {
        if (!property.lookup)
            throw std::runtime_error("binding '" + binding + "' with a key is not a map of numbers");
        return;
    }
    if (isText)
    {
        if (!property.toText)
            throw std::runtime_error("binding '" + binding + "' is not a text, it cannot be compared with '" + text + "'");
        return;
    }
    if (compare == Compare::None && !property.isSet)
        throw std::runtime_error("binding '" + binding + "' has a type that cannot be tested (not a container, not bool)");
    if (compare != Compare::None && !property.toNumber)
        throw std::runtime_error("binding '" + binding + "' is not a number, it cannot be compared");
}

bool Condition_Compare::OnTestCondition(STAI_NodeContext& context) const
{
    const STAI_PropertyDef& property = *GetPropertyRefs()[0].property;
    const void* bound = GetRefValue(context, 0);

    if (isText)
        return property.toText(bound) == text;

    double entry = 0.0;
    const bool hasEntry = hasKey && property.lookup(bound, key, entry);
    if (compare == Compare::None)
        return hasKey ? hasEntry && entry != 0.0 : property.isSet(bound);

    const double number = hasKey ? entry : property.toNumber(bound);
    switch (compare)
    {
        case Compare::Equal: return number == value;
        case Compare::Lesser: return number < value;
        case Compare::LesserOrEqual: return number <= value;
        case Compare::Greater: return number > value;
        default: return number >= value;
    }
}

std::string Condition_Compare::GetDebugInfo() const
{
    std::string info = binding;
    if (hasKey)
    {
        std::ostringstream out;
        out << "[" << key << "]";
        info += out.str();
    }
    if (isText)
        info += " " + compareText + " '" + text + "'";
    else if (compare != Compare::None)
    {
        std::ostringstream out;
        out << " " << compareText << " " << value;
        info += out.str();
    }
    return info;
}
