#pragma once
#include <string>

class PlayerbotAI;

namespace ai
{
    class PlayerbotAIAware 
    {
    public:
        PlayerbotAIAware(PlayerbotAI* const ai) : ai(ai) { }
        virtual ~PlayerbotAIAware() = default;
        virtual std::string GetName() { return std::string(); }
    protected:
        PlayerbotAI* ai;
    };
}