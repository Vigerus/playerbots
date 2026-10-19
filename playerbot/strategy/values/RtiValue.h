#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
    // icons a bot attacks (rti) / crowd-controls (rti cc): up to 8 raid target icon names, in priority order
    constexpr size_t MAX_RTI_ICONS = 8;

    // "skull, cross,square" -> {"skull", "cross", "square"}: lower case, known icon names only (the others go to
    // invalid), no duplicates, at most MAX_RTI_ICONS; "none" is the empty list
    std::vector<std::string> ParseRtiList(const std::string& text, std::vector<std::string>* invalid = nullptr);
    // the list as saved and shown: "skull,cross,square", "none" when empty
    std::string FormatRtiList(const std::vector<std::string>& rtiList);

    class RtiValue : public ManualSetValue<std::vector<std::string>>
	{
	public:
        RtiValue(PlayerbotAI* ai);
        virtual std::string Save();
        virtual bool Load(std::string text);
    };

    class RtiCcValue : public ManualSetValue<std::vector<std::string>>
    {
    public:
        RtiCcValue(PlayerbotAI* ai);

        virtual std::string Save();
        virtual bool Load(std::string text);
    };
}
