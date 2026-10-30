#pragma once
#include <string>

// the trees are looked up next to it: main points it into the repository
inline std::string _D_AIPLAYERBOT_CONFIG = "aiplayerbot.conf";

struct HostPlayerbotConfig
{
    std::string GetConfigString(const std::string& key, const std::string& fallback) const { return fallback; }
};
inline HostPlayerbotConfig hostPlayerbotConfig;
#define sPlayerbotAIConfig hostPlayerbotConfig
