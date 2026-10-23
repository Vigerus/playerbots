#pragma once

#include "Platform/Define.h"

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

class Player;
struct SpellEntry;

namespace ai
{
    // The totems a shaman bot keeps up, one per school (fire, earth, water, air, the core's TotemSlot order), each a
    // totem spell (its first rank: the bot casts the highest rank it knows) or none (0).
    // One outdoor set, the default, and a set per dungeon stage (InstanceSubsystem): in a dungeon the set of the stage
    // the master stands in (the bot's own without the master on its map) is used when it has one, else the outdoor
    // set. Selecting a totem changes the set in use: the stage's, which starts as a copy of the outdoor set, or the
    // outdoor set outside dungeons, in a dungeon without stages or outside every stage.
    // Table ai_playerbot_totems (characters database, the outdoor set is map 0 stage 0: map 0 is never a dungeon),
    // loaded on first use, written on every change; WoWAddonInspectBot selects them ("BOT\ttotem <school> <spell>").
    // Used from the bot's update (map thread).
    class PlayerbotTotems
    {
    public:
        // per school (TotemSlot) the first rank of the totem spell, 0 none
        using TotemSet = std::array<uint32, 4>;

        // where a set is saved: (map, stage), the outdoor set is (0, 0)
        using SetKey = std::pair<uint32, uint32>;
        static constexpr SetKey outdoorKey = { 0, 0 };

        // the set the bot's totems come from now (see above)
        static SetKey GetCurrentKey(Player* bot, Player* master);
        // the set of the key: its own, else the outdoor set
        const TotemSet& GetSet(Player* bot, const SetKey& key);
        // the key has a set of its own (the outdoor key always has)
        bool HasOwnSet(Player* bot, const SetKey& key);
        // selects the totem (a rank of a totem spell of the school, 0 none) of the school (TotemSlot) in the key's set;
        // false when the spell is no totem of that school
        bool Select(Player* bot, const SetKey& key, uint8 school, uint32 spellId);
        // counts the changes (Select): TotemProvider_GlobalTask resolves its totems again when it differs from the
        // one it last saw
        uint32 GetVersion() const { return version; }

        // the school (TotemSlot) of a totem spell, TOTEM_SLOT_NONE for other spells
        static uint8 GetTotemSchool(const SpellEntry* spellInfo);
        // the totems of the school the bot knows: the first rank of each, in spell book order
        static std::vector<uint32> GetKnownTotems(Player* bot, uint8 school);
        // the highest rank of the totem (any rank) the bot knows, 0 none
        static uint32 GetHighestKnownRank(Player* bot, uint32 spellId);
        // "fire", "earth", "water", "air"; school from the name, TOTEM_SLOT_NONE for others
        static const char* GetSchoolName(uint8 school);
        static uint8 ParseSchool(const std::string& name);
        // "outdoor" or "<dungeon name> stage <n> (<stage name>)", for the addon
        static std::string DescribeKey(const SetKey& key);

    private:
        void Load(Player* bot);
        void Save(Player* bot, const SetKey& key);

        bool loaded = false;
        uint32 version = 0;
        std::map<SetKey, TotemSet> sets;    // the outdoor set always
    };
}
