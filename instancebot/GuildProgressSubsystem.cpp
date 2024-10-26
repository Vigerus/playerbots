#include "GuildProgressSubsystem.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "GameEvents/GlobalEventSubsystem.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Globals/ObjectMgr.h"
#include "Globals/SharedDefines.h"

#include "playerbot/ItemID.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <boost/assign/list_of.hpp>

#define ITEM_TYPE_ITEM 0
#define ITEM_TYPE_BUFF 1

struct ProgressSet
{
    uint32 id;
    std::vector<uint8> eligible;
};

static std::vector<ProgressSet> approvedItems = {

    { ITEM_GREAT_RAGE_POTION, // # 5633 - Great Rage Potion
    { CLASS_WARRIOR }},
// Protection Potions
    {ITEM_SHADOW_PROTECTION_POTION, // #  6048 - Shadow Protection Potion
    {}},
    {ITEM_FIRE_PROTECTION_POTION, // #  6049 - Fire Protection Potion
    {}},
    {ITEM_FROST_PROTECTION_POTION, // #  6050 - Frost Protection Potion
    {}},
    {ITEM_NATURE_PROTECTION_POTION, // #  6052 - Nature Protection Potion
    {}},


    {ITEM_ELIXIR_OF_GIANTS, // #  9206 - Elixir of Giants
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},

    {ITEM_ELIXIR_OF_SHADOW_POWER, // # 9264 - Elixir of Shadow Power
    { CLASS_WARLOCK, CLASS_PRIEST }},

    {ITEM_MIGHTY_RAGE_POTION, // # 13442 - Mighty Rage Potion
    { CLASS_WARRIOR }},
    {ITEM_SUPERIOR_MANA_POTION, // # 13442 - Superior Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_ELIXIR_OF_SUPERIOR_DEFENSE, // # 13445 - Elixir of Superior Defense
    { CLASS_WARRIOR, CLASS_DRUID }},
    {ITEM_MAJOR_HEALING_POTION, // # 13446 - Major Healing Potion;
    {}},
    {ITEM_ELIXIR_OF_THE_MONGOOSE, // # 13452 - Elixir of the Mongoose
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_GREATER_ARCANE_ELIXIR, // # 13454 - Greater Arcane Elixir
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_GREATER_STONESHIELD_POTION, // # 13455 - Greater Stoneshield Potion
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},

// Greater Protection Potions
    {ITEM_GREATER_FROST_PROTECTION_POTION, // # 13456 - Greater Frost Protection Potion
    {}},
    {ITEM_GREATER_FIRE_PROTECTION_POTION, // # 13457 - Greater Fire Protection Potion
    {}},
    {ITEM_GREATER_NATURE_PROTECTION_POTION, // # 13458 - Greater Nature Protection Potion
    {}},
    {ITEM_GREATER_SHADOW_PROTECTION_POTION, // # 13459 - Greater Shadow Protection Potion
    {}},
    {ITEM_GREATER_ARCANE_PROTECTION_POTION, // # 13461 - Greater Arcane Protection Potion
    {}},

// Flasks:
    {ITEM_FLASK_OF_THE_TITANS, // # 13511 - Flask of the Titans
    {}},
    {ITEM_FLASK_OF_DISTILLED_WISDOM, // # 13511 - Flask of Distilled Wisdom
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_HUNTER }},
    {ITEM_FLASK_OF_SUPREME_POWER, // # 13512 - Flask of Supreme Power
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_FLASK_OF_CHROMATIC_RESISTANCE, // # 13513 - Flask of Chromatic Resistance
    {}},

    {ITEM_AQUAL_QUINTESSENCE, // # 17333 - Aqual Quintessence
    {}},

    {ITEM_MAJOR_MANA_POTION, // # 17580 - Major Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_MAJOR_TROLLS_BLOOD_POTION, // # 20004 Major Troll's Blood Potion
    {}},

    {ITEM_MAGEBLOOD_POTION, // # 20007 - Mageblood Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_ZANDALAR_SIGNET_OF_MOJO, // # 20076 - Zandalar Signet of Mojo
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_ZANDALAR_SIGNET_OF_MIGHT, // # 20077 - Zandalar Signet of Might
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_ZANDALAR_SIGNET_OF_SERENITY, // # 20078 - Zandalar Signet of Serenity
    { CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},

    {ITEM_SPIRIT_OF_ZANZA, // # 20079 - Spirit of Zanza
    {}},
    {ITEM_SHEEN_OF_ZANZA, // # 20080 - Sheen of Zanza
    {}},
    {ITEM_SWIFTNESS_OF_ZANZA, // # 20081 - Swiftness of Zanza
    {}},

    {ITEM_BRILLIANT_MANA_OIL, // # 20748 - Brilliant Mana Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_BRILLIANT_WIZARD_OIL, // # 20749 - Brilliant Wizard Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},

    {ITEM_ELIXIR_OF_GREATER_FIREPOWER, // # 21546 - Elixir of Greater Firepower
    { CLASS_MAGE, CLASS_WARLOCK }}
};

static std::vector<ProgressSet> approvedBuffs = {
    {10767, // # 10767 - Rising Spirit (Agamaggan)}
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {15123, // # 15123 - Resist Fire
    {}},
    {15366, // # 15366 - Songflower Serenade
    {}},
    {16609, // # 16609 - Warchief Blessing
    {}},
    {16610, // # 16610 - Razorhide (Agamaggan)
    { CLASS_WARRIOR, CLASS_DRUID }},
    {16612, // # 16612 - Agamaggan's Strength (Agamaggan)
    { CLASS_WARRIOR, CLASS_DRUID, CLASS_ROGUE, CLASS_SHAMAN, CLASS_PALADIN, CLASS_HUNTER }},
    {16618, // # 16618 - Spirit of the Wind (Agamaggan)
    {}},
    {17013, // # 17013 - Agamaggan's Agility (Agamaggan)
    { CLASS_WARRIOR, CLASS_DRUID, CLASS_ROGUE, CLASS_SHAMAN, CLASS_PALADIN, CLASS_HUNTER }},
    {22888, // # 22888 - Rallying Cry of the Dragonslayer
    {}},
    {24425, // # 24425 - Spirit of Zandalar
    {}}
};


static bool IsItemViable(uint32 itemId, const std::vector<uint32>& fullSet)
{
    switch (itemId)
    {
        case ITEM_SHADOW_PROTECTION_POTION:
            return std::find(fullSet.begin(), fullSet.end(), ITEM_GREATER_SHADOW_PROTECTION_POTION) == fullSet.end();
        case ITEM_NATURE_PROTECTION_POTION:
            return std::find(fullSet.begin(), fullSet.end(), ITEM_GREATER_NATURE_PROTECTION_POTION) == fullSet.end();
        case ITEM_FIRE_PROTECTION_POTION:
            return std::find(fullSet.begin(), fullSet.end(), ITEM_GREATER_FIRE_PROTECTION_POTION) == fullSet.end();
//         case ITEM_ARCANE_PROTECTION_POTION:
//             return std::find(fullSet.begin(), fullSet.end(), ITEM_GREATER_ARCANE_PROTECTION_POTION) == fullSet.end();

        case ITEM_SUPERIOR_MANA_POTION:
            return std::find(fullSet.begin(), fullSet.end(), ITEM_MAJOR_MANA_POTION) == fullSet.end();

        case ITEM_GREAT_RAGE_POTION:
            return std::find(fullSet.begin(), fullSet.end(), ITEM_MIGHTY_RAGE_POTION) == fullSet.end();
    }

    return true;
}

GuildProgressSubsystem::GuildProgressSubsystem()
{
    Init();

    GlobalEventSubsystem::instance().GetOnItemCreatedDelegate().Add([&](Player* player, uint32 itemId)
    {
        OnItemCreated(player, itemId);
    });

    GlobalEventSubsystem::instance().GetOnAuraAppliedDelegate().Add([&](Unit* unit, uint32 spellId)
    {
        OnAuraApplied(unit, spellId);
    });
}

GuildProgressSubsystem::~GuildProgressSubsystem()
{

}

void GuildProgressSubsystem::Init()
{

}

void GuildProgressSubsystem::Load(const ConfigAccess& config)
{
    auto results = CharacterDatabase.PQuery("SELECT `guild_id`,`item_type`,`item_id` FROM `ai_playerbot_guild_progress`");
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 guild_id = fields[0].GetUInt32();
            uint32 item_type = fields[1].GetUInt32();
            uint32 item_id = fields[2].GetUInt32();

            if (item_type == ITEM_TYPE_ITEM)
            {
                guildItemsMap[guild_id].push_back(item_id);
            }
            else
            {
                guildBuffsMap[guild_id].push_back(item_id);
            }

        } while (results->NextRow());

        for (auto& el : guildItemsMap)
        {
            BuildReducedSet(el.second, guildReducedItemsMap[el.first]);
        }
    }
}

void GuildProgressSubsystem::BuildReducedSet(const std::vector<uint32>& fullSet, std::vector<uint32>& reducedSet)
{
    reducedSet.clear();

    for (uint32 itemId : fullSet)
    {
        if (IsItemViable(itemId, fullSet))
        {
            reducedSet.emplace_back(itemId);
        }
    }
}

void GuildProgressSubsystem::Save(uint32 guildId, uint32 itemType, uint32 itemId)
{
    CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_guild_progress` (`guild_id`, `item_type`, `item_id`) VALUES ('%u', '%u', '%u')", guildId, itemType, itemId);
}

void GuildProgressSubsystem::OnItemCreated(Player* player, uint32 itemId)
{
    if (player == nullptr)
    {
        return;
    }

    auto it = std::find_if(approvedItems.begin(), approvedItems.end(), [&](const ProgressSet& el) { return el.id == itemId; });
    if (it == approvedItems.end())
    {
        return;
    }

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemId);

    if (pProto == nullptr)
    {
        return;
    }

    uint32 guildId = player->GetGuildId();

    if (Guild* guild = sGuildMgr.GetGuildById(guildId))
    {
        std::vector<uint32>& guildItems = guildItemsMap[guildId];

        if (std::find(guildItems.begin(), guildItems.end(), itemId) == guildItems.end())
        {
            std::ostringstream ss;
            ss << "New item " << pProto->Name1 << " added to the guild pool!";
            guildItems.push_back(itemId);

            // recreate the reduced map
            BuildReducedSet(guildItems, guildReducedItemsMap[guildId]);

            Save(guildId, ITEM_TYPE_ITEM, itemId);
            guild->BroadcastEvent(GE_MOTD, ss.str().c_str());
        }
    }
}

void GuildProgressSubsystem::OnAuraApplied(Unit* unit, uint32 spellId)
{
    if (unit == nullptr)
    {
        return;
    }

    const Player* player = unit->GetControllingPlayer(true);

    if (player == nullptr)
    {
        return;
    }

    auto it = std::find_if(approvedBuffs.begin(), approvedBuffs.end(), [&](const ProgressSet& el) { return el.id == spellId; });
    if (it == approvedBuffs.end())
    {
        return;
    }

    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);

    if (!spellInfo)
    {
        return;
    }

    uint32 guildId = player->GetGuildId();

    if (Guild* guild = sGuildMgr.GetGuildById(guildId))
    {
        std::vector<uint32>& guildBuffs = guildBuffsMap[guildId];

        if (std::find(guildBuffs.begin(), guildBuffs.end(), spellId) == guildBuffs.end())
        {
            std::ostringstream ss;
            ss << "New iaura " << spellInfo->SpellName << " added to the guild pool!";
            guildBuffs.push_back(spellId);
            Save(guildId, ITEM_TYPE_BUFF, spellId);
            guild->BroadcastEvent(GE_MOTD, ss.str().c_str());
        }
    }
}

std::vector<uint32> GuildProgressSubsystem::GetGuildBuffs(uint32 guildId) const
{
    auto it = guildBuffsMap.find(guildId);

    if (it == guildBuffsMap.end())
    {
        return { };
    }

    return it->second;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildItems(uint32 guildId) const
{
    auto it = guildReducedItemsMap.find(guildId);

    if (it == guildReducedItemsMap.end())
    {
        return { };
    }

    return it->second;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildBuffsForClass(uint32 guildId, uint8 classId) const
{
    auto it = guildBuffsMap.find(guildId);

    if (it == guildBuffsMap.end())
    {
        return { };
    }

    std::vector<uint32> buffs;
    buffs.reserve(it->second.size());

    for (uint32 spellId : it->second)
    {
        auto it = std::find_if(approvedBuffs.begin(), approvedBuffs.end(), [&](const ProgressSet& el) { return el.id == spellId; });

        if (it == approvedBuffs.end())
        {
            continue;
        }

        if (it->eligible.size() && std::find(it->eligible.begin(), it->eligible.end(), classId) == it->eligible.end())
        {
            continue;
        }

        buffs.emplace_back(spellId);
    }

    return buffs;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildItemsForClass(uint32 guildId, uint8 classId) const
{
    auto it = guildReducedItemsMap.find(guildId);

    if (it == guildReducedItemsMap.end())
    {
        return { };
    }

    std::vector<uint32> items;
    items.reserve(it->second.size());

    for (uint32 itemId : it->second)
    {
        auto it = std::find_if(approvedItems.begin(), approvedItems.end(), [&](const ProgressSet& el) { return el.id == itemId; });

        if (it == approvedItems.end())
        {
            continue;
        }

        if (it->eligible.size() && std::find(it->eligible.begin(), it->eligible.end(), classId) == it->eligible.end())
        {
            continue;
        }

        items.emplace_back(itemId);
    }

    return items;
}