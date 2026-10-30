#include "STAI_BuiltinConsiderations.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

using namespace ai;

namespace
{
    float ParseFloat(const STAI_TaskConfig& config, const std::string& name, float fallback, bool required = false)
    {
        const std::string value = config.get<std::string>("<xmlattr>." + name, "");
        if (value.empty())
        {
            if (required)
                throw std::runtime_error(name + "=\"...\" is missing");
            return fallback;
        }
        char* end = nullptr;
        const double number = std::strtod(value.c_str(), &end);
        if (*end)
            throw std::runtime_error(name + "=\"...\" needs a number, got '" + value + "'");
        return float(number);
    }

    std::vector<std::pair<float, float>> ParseKeys(const STAI_TaskConfig& config)
    {
        std::vector<std::pair<float, float>> keys;
        for (const auto& [name, child] : config)
        {
            if (name == "Key")
                keys.emplace_back(ParseFloat(child, "x", 0.0f, true), ParseFloat(child, "y", 0.0f, true));
            else if (name != "<xmlattr>" && name != "<xmlcomment>" && name != "Comment")
                throw std::runtime_error("unexpected <" + name + "> in <Consideration> (expected <Key x=\"...\" y=\"...\"/>)");
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }
}

void Consideration_Constant::Validate(const STAI_TaskConfig& config)
{
    ParseFloat(config, "value", 0.0f, true);
}

std::unique_ptr<STAI_ConsiderationBase> Consideration_Constant::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<Consideration_Constant>(ParseFloat(config, "value", 0.0f, true));
}

std::string Consideration_Constant::GetDebugInfo() const
{
    std::ostringstream out;
    out << "score " << value;
    return out.str();
}

void Consideration_Number::Validate(const STAI_TaskConfig& config)
{
    if (config.get<std::string>("<xmlattr>.binding", "").empty())
        throw std::runtime_error("<Consideration class=\"Consideration_Number\"> needs a binding=\"Root|...\" (a number property)");
    if (ParseFloat(config, "min", 0.0f) > ParseFloat(config, "max", 1.0f))
        throw std::runtime_error("min=\"...\" is above max=\"...\"");
    ParseKeys(config);
}

std::unique_ptr<STAI_ConsiderationBase> Consideration_Number::Create(const STAI_TaskConfig& config)
{
    auto consideration = std::make_unique<Consideration_Number>();
    consideration->binding = config.get<std::string>("<xmlattr>.binding", "");
    consideration->min = ParseFloat(config, "min", 0.0f);
    consideration->max = ParseFloat(config, "max", 1.0f);
    consideration->keys = ParseKeys(config);
    consideration->AddPropertyRef(consideration->binding);
    return consideration;
}

void Consideration_Number::ValidatePropertyRef(size_t index, const STAI_PropertyDef& property) const
{
    if (!property.toNumber)
        throw std::runtime_error("binding '" + binding + "' is not a number");
}

float Consideration_Number::CalculateScore(STAI_NodeContext& context) const
{
    const double number = GetPropertyRefs()[0].property->toNumber(GetRefValue(context, 0));
    const float input = max > min ? std::clamp(float((number - min) / (max - min)), 0.0f, 1.0f) : (number >= max ? 1.0f : 0.0f);

    if (keys.empty())
        return input;
    if (input <= keys.front().first)
        return keys.front().second;
    if (input >= keys.back().first)
        return keys.back().second;

    for (size_t i = 1; i < keys.size(); ++i)
    {
        const auto& [x0, y0] = keys[i - 1];
        const auto& [x1, y1] = keys[i];
        if (input <= x1)
            return x1 > x0 ? y0 + (y1 - y0) * (input - x0) / (x1 - x0) : y1;
    }
    return keys.back().second;
}

std::string Consideration_Number::GetDebugInfo() const
{
    std::ostringstream out;
    out << binding << " in " << min << ".." << max;
    if (!keys.empty())
        out << " curve " << keys.size() << " keys";
    return out.str();
}
