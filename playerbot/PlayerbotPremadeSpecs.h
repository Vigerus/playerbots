#pragma once

#include "playerbot/Talentspec.h"

#include <sstream>
#include <vector>

class Player;

namespace ai
{
    // The premade talent specs of aiplayerbot.conf (AiPlayerbot.PremadeSpecName / Link / Hint, PlayerbotAIConfig
    // classSpecs) for one bot: which of them it has, and applying one (the WoWAddonInspectBot spec box).
    class PlayerbotPremadeSpecs
    {
    public:
        // the premade specs of the bot's class, in config order
        static const std::vector<TalentPath>& GetPaths(const Player* bot);
        static const TalentPath* Find(const Player* bot, int pathId);

        // the path's spec for the bot's level: the first link with at least the bot's talent points (else the last),
        // cut to the points the bot has
        static TalentSpec GetSpecForLevel(Player* bot, const TalentPath& path);

        // the premade spec the bot has: the one whose spec for its level are exactly its talents, else the only one
        // whose hint is the bot's spec (SpecProvider_GlobalTask::GetSpecName); -1 when none (or several) match
        static int GetCurrentPathId(Player* bot);

        // gives the bot the path's talents for its level (unlearns the others); false for an unknown path
        static bool Apply(Player* bot, int pathId, std::ostringstream& out);
    };
}
