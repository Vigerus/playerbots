#pragma once
#include "playerbot/strategy/Value.h"

namespace ai
{
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
