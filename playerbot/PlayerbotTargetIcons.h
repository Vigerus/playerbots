#pragma once

#include "Platform/Define.h"

#include <string>
#include <vector>

class Player;
class Unit;

namespace ai
{
    // The raid target icons a bot attacks first (rti: kill order, a list in priority order) and the one icon it
    // crowd-controls (rti cc: polymorph, shackle, ...), set by the "rti" chat command:
    //   rti skull,cross       the first living unit marked skull, else cross, is the bot's target
    //   rti cc circle         the unit marked circle is crowd-controlled (one icon)
    //   rti none / rti cc none   no icons
    //   rti ? / rti cc ?      the bot whispers its icons and the unit they mark now
    // Defaults skull / moon. Kept in ai_playerbot_db_store (characters database, preset "", key "value") as the
    // legacy "rti" / "rti cc" values ("rti>skull,cross"), so orders saved by the old engine load again. Loaded on
    // first use, written on every change. Used from the bot's update (map thread).
    class PlayerbotTargetIcons
    {
    public:
        // icons per list at most
        static constexpr size_t maxIcons = 8;

        const std::vector<std::string>& GetRti(Player* bot) { Load(bot); return rti; }
        // the crowd control icon, empty for none
        const std::string& GetRtiCc(Player* bot) { Load(bot); return rtiCc; }
        // the crowd control icon as a list (none or one), for the code that looks up marks of lists
        std::vector<std::string> GetRtiCcList(Player* bot) { Load(bot); return rtiCc.empty() ? std::vector<std::string>() : std::vector<std::string>{ rtiCc }; }
        void SetRti(Player* bot, const std::vector<std::string>& icons);
        void SetRtiCc(Player* bot, const std::string& icon);
        // counts the changes (SetRti / SetRtiCc): the providers that use the icons (TargetProvider_GlobalTask, CCProvider_GlobalTask)
        // choose again on their next update when it differs from the one they last saw
        uint32 GetVersion() const { return version; }

        // the first living unit of the group marked with one of the icons, in their order; null without one
        static Unit* GetMarkedUnit(Player* bot, const std::vector<std::string>& icons);

        // "skull, cross,square" -> {"skull", "cross", "square"}: lower case, known icon names only (the others go to
        // invalid), no duplicates, at most maxIcons; "none" is the empty list
        static std::vector<std::string> Parse(const std::string& text, std::vector<std::string>* invalid = nullptr);
        // the list as saved and shown: "skull,cross,square", "none" when empty
        static std::string Format(const std::vector<std::string>& icons);

    private:
        void Load(Player* bot);
        void Save(Player* bot);

        bool loaded = false;
        uint32 version = 0;
        std::vector<std::string> rti = { "skull" };
        std::string rtiCc = "moon";
    };
}
