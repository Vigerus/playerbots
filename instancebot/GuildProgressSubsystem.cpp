#include "GuildProgressSubsystem.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "GameEvents/GlobalEventSubsystem.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Globals/ObjectMgr.h"
#include "Globals/SharedDefines.h"

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
// Protection Potions
    {6048, // #  6048 - Shadow Protection Potion
    {}},
    {6049, // #  6049 - Fire Protection Potion
    {}},
    {6050, // #  6050 - Frost Protection Potion
    {}},
    {6052, // #  6052 - Nature Protection Potion
    {}},

    {9206, // #  9206 - Elixir of Giants
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},
    {13442, // # 13442 - Mighty Rage Potion
    { CLASS_WARRIOR }},
    {13445, // # 13445 - Elixir of Superior Defense
    { CLASS_WARRIOR, CLASS_DRUID }},
    {13446, // # 13446 - Major Healing Potion;
    {}},
    {13452, // # 13452 - Elixir of the Mongoose
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {13454, // # 13454 - Greater Arcane Elixir
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {13455, // # 13455 - Greater Stoneshield Potion
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},

// Greater Protection Potions
    {13456, // # 13456 - Greater Frost Protection Potion
    {}},
    {13457, // # 13457 - Greater Fire Protection Potion
    {}},
    {13458, // # 13458 - Greater Nature Protection Potion
    {}},
    {13459, // # 13459 - Greater Shadow Protection Potion
    {}},
    {13461, // # 13461 - Greater Arcane Protection Potion
    {}},

// Flasks:
    {13510, // # 13511 - Flask of Titans
    {}},
    {13511, // # 13511 - Flask of Distilled Wisdom
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_HUNTER }},
    {13512, // # 13512 - Flask of Supreme Power
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_SHAMAN, CLASS_DRUID }},
    {13513, // # 13513 - Flask of Chromatic Resistance
    {}},

    {17333, // # 17333 - Aqual Quintessence
    {}},

    {20076, // # 20076 - Zandalar Signet of Mojo
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {20077, // # 20077 - Zandalar Signet of Might
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {20078, // # 20078 - Zandalar Signet of Serenity
    { CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},

    {20079, // # 20079 - Spirit of Zanza
    {}},
    {20080, // # 20080 - Sheen of Zanza
    {}},
    {20081, // # 20081 - Swiftness of Zanza
    {}},

    {20748, // # 20748 - Brilliant Mana Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {20749, // # 20749 - Brilliant Wizard Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},

    {21546, // # 21546 - Elixir of Greater Firepower
    { CLASS_MAGE, CLASS_WARLOCK }}
};

static std::vector<ProgressSet> approvedBuffs = {
    {15123, // # 15123 - Resist Fire
    {}},
    {15366, // # 15366 - Songflower Serenade
    {}},
    {16609, // # 16609 - Warchief Blessing
    {}},
    {22888, // # 22888 - Rallying Cry of the Dragonslayer
    {}},
    {24425, // # 24425 - Spirit of Zandalar
    {}}
};

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
        std::list<std::string> values;
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
    auto it = guildItemsMap.find(guildId);

    if (it == guildItemsMap.end())
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
    auto it = guildItemsMap.find(guildId);

    if (it == guildItemsMap.end())
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