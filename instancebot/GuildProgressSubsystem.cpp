#include "GuildProgressSubsystem.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "GameEvents/GlobalEventSubsystem.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Globals/ObjectMgr.h"
#include "Globals/SharedDefines.h"
#include "Globals/ObjectAccessor.h"
#include "Chat/Chat.h"
#include "Maps/Map.h"

#include "playerbot/ItemID.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <sstream>

#define ITEM_TYPE_ITEM 0
#define ITEM_TYPE_BUFF 1

#define QUEST_IN_DREAMS 5944
#define QUEST_SIGNETS_OF_THE_ZANDALAR 8246

#define CHECK_INTERVAL 10       // seconds between two checks of a guild member's auras / items
#define CHUNK_SIZE 220

static std::vector<ItemProgressSet> ConsumedItems = {

    { ITEM_GREAT_RAGE_POTION, ItemAcquireMethod::Continuous, // # 5633 - Great Rage Potion
    { CLASS_WARRIOR }},
// Protection Potions
    {ITEM_SHADOW_PROTECTION_POTION, ItemAcquireMethod::Continuous, // #  6048 - Shadow Protection Potion
    {}},
    {ITEM_FIRE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // #  6049 - Fire Protection Potion
    {}},
    {ITEM_FROST_PROTECTION_POTION, ItemAcquireMethod::Continuous, // #  6050 - Frost Protection Potion
    {}},
    {ITEM_NATURE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // #  6052 - Nature Protection Potion
    {}},


    {ITEM_ELIXIR_OF_GIANTS, ItemAcquireMethod::Continuous, // #  9206 - Elixir of Giants
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},

    {ITEM_ELIXIR_OF_SHADOW_POWER, ItemAcquireMethod::Continuous, // # 9264 - Elixir of Shadow Power
    { CLASS_WARLOCK, CLASS_PRIEST }},

    {ITEM_MIGHTY_RAGE_POTION, ItemAcquireMethod::Continuous, // # 13442 - Mighty Rage Potion
    { CLASS_WARRIOR }},
    {ITEM_SUPERIOR_MANA_POTION, ItemAcquireMethod::Continuous, // # 13442 - Superior Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_ELIXIR_OF_SUPERIOR_DEFENSE, ItemAcquireMethod::Continuous, // # 13445 - Elixir of Superior Defense
    { CLASS_WARRIOR, CLASS_DRUID }},
    {ITEM_MAJOR_HEALING_POTION, ItemAcquireMethod::Continuous, // # 13446 - Major Healing Potion;
    {}},
    {ITEM_ELIXIR_OF_THE_MONGOOSE, ItemAcquireMethod::Continuous, // # 13452 - Elixir of the Mongoose
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_GREATER_ARCANE_ELIXIR, ItemAcquireMethod::Continuous, // # 13454 - Greater Arcane Elixir
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_GREATER_STONESHIELD_POTION, ItemAcquireMethod::Continuous, // # 13455 - Greater Stoneshield Potion
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},

// Greater Protection Potions
    {ITEM_GREATER_FROST_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13456 - Greater Frost Protection Potion
    {}},
    {ITEM_GREATER_FIRE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13457 - Greater Fire Protection Potion
    {}},
    {ITEM_GREATER_NATURE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13458 - Greater Nature Protection Potion
    {}},
    {ITEM_GREATER_SHADOW_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13459 - Greater Shadow Protection Potion
    {}},
    {ITEM_GREATER_ARCANE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13461 - Greater Arcane Protection Potion
    {}},

// Flasks:
    {ITEM_FLASK_OF_THE_TITANS, ItemAcquireMethod::Continuous, // # 13511 - Flask of the Titans
    {}},
    {ITEM_FLASK_OF_DISTILLED_WISDOM, ItemAcquireMethod::Continuous, // # 13511 - Flask of Distilled Wisdom
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_HUNTER }},
    {ITEM_FLASK_OF_SUPREME_POWER, ItemAcquireMethod::Continuous, // # 13512 - Flask of Supreme Power
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_FLASK_OF_CHROMATIC_RESISTANCE, ItemAcquireMethod::Continuous, // # 13513 - Flask of Chromatic Resistance
    {}},

    {ITEM_AQUAL_QUINTESSENCE, ItemAcquireMethod::Continuous, // # 17333 - Aqual Quintessence
    {}},

    {ITEM_MAJOR_MANA_POTION, ItemAcquireMethod::Continuous, // # 17580 - Major Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_MAJOR_TROLLS_BLOOD_POTION, ItemAcquireMethod::Continuous, // # 20004 Major Troll's Blood Potion
    {}},

    {ITEM_MAGEBLOOD_POTION, ItemAcquireMethod::Continuous, // # 20007 - Mageblood Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},

    {ITEM_SPIRIT_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20079 - Spirit of Zanza
    {}},
    {ITEM_SHEEN_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20080 - Sheen of Zanza
    {}},
    {ITEM_SWIFTNESS_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20081 - Swiftness of Zanza
    {}},

    {ITEM_BRILLIANT_MANA_OIL, ItemAcquireMethod::Continuous, // # 20748 - Brilliant Mana Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_BRILLIANT_WIZARD_OIL, ItemAcquireMethod::Continuous, // # 20749 - Brilliant Wizard Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},

    {ITEM_ELIXIR_OF_GREATER_FIREPOWER, ItemAcquireMethod::Continuous, // # 21546 - Elixir of Greater Firepower
    { CLASS_MAGE, CLASS_WARLOCK }}
};

static std::vector<ItemProgressSet> CraftedItems = {
    {ITEM_GIRDLE_OF_THE_DAWN, ItemAcquireMethod::OnDemand, // # 19051 - Girdle of the Dawn
    {}}
};

// every reward choice of a quest unlocks for the guild when one member rewards it, whichever one was picked
static std::vector<ItemProgressSet> QuestItems = {
    {ITEM_ZANDALAR_SIGNET_OF_MOJO, ItemAcquireMethod::OnDemand, // # 20076 - Zandalar Signet of Mojo
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, QUEST_SIGNETS_OF_THE_ZANDALAR},
    {ITEM_ZANDALAR_SIGNET_OF_MIGHT, ItemAcquireMethod::OnDemand, // # 20077 - Zandalar Signet of Might
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, QUEST_SIGNETS_OF_THE_ZANDALAR},
    {ITEM_ZANDALAR_SIGNET_OF_SERENITY, ItemAcquireMethod::OnDemand, // # 20078 - Zandalar Signet of Serenity
    { CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, QUEST_SIGNETS_OF_THE_ZANDALAR},

    {ITEM_MARK_OF_FORDRING, ItemAcquireMethod::OnDemand, // # 15411 - Mark of Fordring
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, QUEST_IN_DREAMS},
    {ITEM_ORNATE_ADAMANTIUM_BREASTPLATE, ItemAcquireMethod::OnDemand, // # 15413 - Ornate Adamantium Breastplate
    { CLASS_WARRIOR, CLASS_PALADIN }, QUEST_IN_DREAMS},
    {ITEM_SHIMMERING_PLATINUM_WARHAMMER, ItemAcquireMethod::OnDemand, // # 15418 - Shimmering Platinum Warhammer
    {}, QUEST_IN_DREAMS},
    {ITEM_SHROUD_OF_THE_EXILE, ItemAcquireMethod::OnDemand, // # 15421 - Shroud of the Exile
    {}, QUEST_IN_DREAMS},
    {ITEM_FORDRINGS_SEAL, ItemAcquireMethod::OnDemand, // # 16058 - Fordring's Seal
    {}, QUEST_IN_DREAMS}
};

// addon tab codes: C consumables, R crafted, Q quest
static const std::vector<std::pair<const std::vector<ItemProgressSet>*, char>> ItemCategories = {
    { &ConsumedItems, 'C' }, { &CraftedItems, 'R' }, { &QuestItems, 'Q' }
};

static std::vector<AuraProgressSet> approvedBuffs = {
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


static const ItemProgressSet* FindItem(uint32 itemId, char* category = nullptr)
{
    for (auto const& [items, code] : ItemCategories)
    {
        auto it = std::find_if(items->begin(), items->end(), [&](const ItemProgressSet& el) { return el.id == itemId; });
        if (it != items->end())
        {
            if (category)
            {
                *category = code;
            }
            return &*it;
        }
    }

    return nullptr;
}

static bool IsEligible(const std::vector<uint8>& eligible, uint8 classId)
{
    return eligible.empty() || std::find(eligible.begin(), eligible.end(), classId) != eligible.end();
}

// names go into the '^' / '~' separated addon payload
static std::string Clean(std::string text)
{
    std::replace(text.begin(), text.end(), '^', ' ');
    std::replace(text.begin(), text.end(), '~', ' ');
    return text;
}

static bool AddAura(Unit* unit, uint32 spellId)
{
    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (!spellInfo)
        return false;

    if (!IsSpellAppliesAura(spellInfo, (1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2)) &&
        !IsSpellHaveEffect(spellInfo, SPELL_EFFECT_PERSISTENT_AREA_AURA))
    {
        return false;
    }

    SpellAuraHolder* holder = CreateSpellAuraHolder(spellInfo, unit, unit);

    for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
    {
        uint8 eff = spellInfo->Effect[i];
        if (eff >= MAX_SPELL_EFFECTS)
            continue;
        if (IsAreaAuraEffect(eff) ||
            eff == SPELL_EFFECT_APPLY_AURA ||
            eff == SPELL_EFFECT_PERSISTENT_AREA_AURA)
        {
            int32 basePoints = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));
            int32 damage = basePoints;
            Aura* aur = CreateAura(spellInfo, SpellEffectIndex(i), &damage, &basePoints, holder, unit);
            holder->AddAura(aur, SpellEffectIndex(i));
        }
    }
    if (!unit->AddSpellAuraHolder(holder))
        delete holder;

    return true;
}

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

    GlobalEventSubsystem::instance().GetOnQuestRewardedDelegate().Add([&](Player* player, uint32 questId)
    {
        OnQuestRewarded(player, questId);
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
    std::lock_guard<std::mutex> lock(m_lock);

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

bool GuildProgressSubsystem::UnlockItem(uint32 guildId, uint32 itemId)
{
    {
        std::lock_guard<std::mutex> lock(m_lock);
        std::vector<uint32>& guildItems = guildItemsMap[guildId];

        if (std::find(guildItems.begin(), guildItems.end(), itemId) != guildItems.end())
        {
            return false;
        }

        guildItems.push_back(itemId);

        // recreate the reduced map
        BuildReducedSet(guildItems, guildReducedItemsMap[guildId]);
    }

    Save(guildId, ITEM_TYPE_ITEM, itemId);
    return true;
}

bool GuildProgressSubsystem::IsItemUnlocked(uint32 guildId, uint32 itemId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildItemsMap.find(guildId);
    return it != guildItemsMap.end() && std::find(it->second.begin(), it->second.end(), itemId) != it->second.end();
}

void GuildProgressSubsystem::UnlockQuest(uint32 guildId, uint32 questId)
{
    Guild* guild = sGuildMgr.GetGuildById(guildId);
    if (guild == nullptr)
    {
        return;
    }

    uint32 unlocked = 0;
    for (const ItemProgressSet& item : QuestItems)
    {
        if (item.questId == questId && UnlockItem(guildId, item.id))
        {
            ++unlocked;
        }
    }

    if (unlocked == 0)
    {
        return;
    }

    Quest const* quest = sObjectMgr.GetQuestTemplate(questId);

    std::ostringstream ss;
    ss << "Quest " << (quest ? quest->GetTitle() : std::to_string(questId)) << " completed: " << unlocked << " items added to the guild pool!";
    guild->BroadcastEvent(GE_MOTD, ss.str().c_str());
}

void GuildProgressSubsystem::OnItemCreated(Player* player, uint32 itemId)
{
    if (player == nullptr)
    {
        return;
    }

    // quest items unlock through their quest only (OnQuestRewarded), not by getting one of them
    char category = 0;
    const ItemProgressSet* item = FindItem(itemId, &category);
    if (item == nullptr || category == 'Q')
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
        if (UnlockItem(guildId, itemId))
        {
            std::ostringstream ss;
            ss << "New item " << pProto->Name1 << " added to the guild pool!";
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

    auto it = std::find_if(approvedBuffs.begin(), approvedBuffs.end(), [&](const AuraProgressSet& el) { return el.id == spellId; });
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
        {
            std::lock_guard<std::mutex> lock(m_lock);
            std::vector<uint32>& guildBuffs = guildBuffsMap[guildId];

            if (std::find(guildBuffs.begin(), guildBuffs.end(), spellId) != guildBuffs.end())
            {
                return;
            }

            guildBuffs.push_back(spellId);
        }

        std::ostringstream ss;
        ss << "New aura " << spellInfo->SpellName[0] << " added to the guild pool!";
        Save(guildId, ITEM_TYPE_BUFF, spellId);
        guild->BroadcastEvent(GE_MOTD, ss.str().c_str());
    }
}

void GuildProgressSubsystem::OnQuestRewarded(Player* player, uint32 questId)
{
    if (player == nullptr || player->GetGuildId() == 0)
    {
        return;
    }

    UnlockQuest(player->GetGuildId(), questId);
}

void GuildProgressSubsystem::ApplyGuildProgress(Player* player)
{
    uint32 guildId = player->GetGuildId();
    uint8 classId = player->getClass();

    // quests rewarded before the guild tracked them
    for (const ItemProgressSet& item : QuestItems)
    {
        if (!IsItemUnlocked(guildId, item.id) && player->GetQuestRewardStatus(item.questId))
        {
            UnlockQuest(guildId, item.questId);
        }
    }

    // no lock held: applying auras / storing items broadcasts OnAuraApplied / OnItemCreated back here
    for (uint32 spellId : GetGuildBuffsForClass(guildId, classId))
    {
        if (!player->HasAura(spellId))
        {
            AddAura(player, spellId);
        }
    }

    for (uint32 itemId : GetGuildItemsForClass(guildId, classId))
    {
        if (player->GetItemByEntry(itemId) == nullptr)
        {
            player->StoreNewItemInBestSlots(itemId, 1);
        }
    }
}

void GuildProgressSubsystem::UpdateMap(Map* map)
{
    time_t now = time(nullptr);

    for (auto const& ref : map->GetPlayers())
    {
        Player* player = ref.getSource();
        if (player == nullptr || !player->IsInWorld() || !player->IsAlive() || player->GetGuildId() == 0)
        {
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(m_lock);
            time_t& next = m_nextCheck[player->GetObjectGuid()];
            if (next > now)
            {
                continue;
            }
            next = now + CHECK_INTERVAL;
        }

        ApplyGuildProgress(player);
    }

    if (!m_hasRequests)
    {
        return;
    }

    std::vector<std::pair<Player*, Request>> requests;
    {
        std::lock_guard<std::mutex> lock(m_requestLock);
        for (auto itr = m_requests.begin(); itr != m_requests.end();)
        {
            if (Player* player = map->GetPlayer(itr->player))
            {
                requests.emplace_back(player, std::move(*itr));
                itr = m_requests.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
        m_hasRequests = !m_requests.empty();
    }

    for (auto const& [player, request] : requests)
    {
        if (player->IsInWorld() && player->GetSession())
        {
            HandleRequest(player, request.msg, request.internal);
        }
    }
}

void GuildProgressSubsystem::OnPlayerLeaveMap(Player* player)
{
    // checked again as soon as it is on its next map
    std::lock_guard<std::mutex> lock(m_lock);
    m_nextCheck.erase(player->GetObjectGuid());
}

bool GuildProgressSubsystem::HandleAddonRequest(Player* player, std::string const& msg)
{
    if (msg.rfind("BOT\tguildprogress ", 0) != 0)
    {
        return false;
    }

    if (player)
    {
        QueueRequest(player->GetObjectGuid(), msg.substr(4), false);
    }
    return true;
}

void GuildProgressSubsystem::QueueRequest(ObjectGuid guid, std::string const& msg, bool internal)
{
    std::lock_guard<std::mutex> lock(m_requestLock);
    m_requests.push_back({ guid, msg, internal });
    m_hasRequests = true;
}

void GuildProgressSubsystem::Reply(ObjectGuid requester, std::string const& text)
{
    QueueRequest(requester, "reply " + text, true);
}

// "guildprogress list", "guildprogress give <item> <member>" from the addon; "spawn <item> <requester>" on the member's
// map and "reply <text>" on the requester's map, queued by the give
void GuildProgressSubsystem::HandleRequest(Player* player, std::string const& msg, bool internal)
{
    std::istringstream in(msg);
    std::string command;
    in >> command;

    if (internal && command == "reply")
    {
        std::string text;
        std::getline(in >> std::ws, text);
        ChatHandler(player).SendSysMessage(text.c_str());
        return;
    }

    if (internal && command == "spawn")
    {
        uint32 itemId = 0;
        uint64 requesterRaw = 0;
        in >> itemId >> requesterRaw;
        ObjectGuid requester(requesterRaw);

        const ItemProgressSet* item = FindItem(itemId);
        ItemPrototype const* proto = ObjectMgr::GetItemPrototype(itemId);
        if (item == nullptr || proto == nullptr)
        {
            return;
        }

        std::ostringstream ss;
        if (!IsEligible(item->eligible, player->getClass()))
        {
            ss << player->GetName() << " cannot use " << proto->Name1 << ".";
        }
        else if (player->HasItemCount(itemId, 1, true))
        {
            ss << player->GetName() << " already has " << proto->Name1 << ".";
        }
        else if (!player->StoreNewItemInBestSlots(itemId, 1))
        {
            ss << player->GetName() << " has no room for " << proto->Name1 << ".";
        }
        else
        {
            ss << proto->Name1 << " given to " << player->GetName() << ".";
        }

        Reply(requester, ss.str());
        return;
    }

    if (internal)
    {
        return;
    }

    std::string word = command;
    in >> command;
    if (word != "guildprogress")
    {
        return;
    }

    if (command == "list")
    {
        SendList(player);
        return;
    }

    if (command == "give")
    {
        uint32 itemId = 0;
        std::string targetName;
        in >> itemId >> targetName;

        uint32 guildId = player->GetGuildId();
        const ItemProgressSet* item = FindItem(itemId);
        if (guildId == 0 || item == nullptr || item->acquireMethod != ItemAcquireMethod::OnDemand || !IsItemUnlocked(guildId, itemId))
        {
            ChatHandler(player).SendSysMessage("Your guild has not acquired that item.");
            return;
        }

        Player* target = ObjectAccessor::FindPlayerByName(targetName.c_str());
        if (target == nullptr || target->GetGuildId() != guildId)
        {
            ChatHandler(player).SendSysMessage("Target a member of your guild who is online.");
            return;
        }

        // stored on the member's own map thread
        QueueRequest(target->GetObjectGuid(), "spawn " + std::to_string(itemId) + " " + std::to_string(player->GetObjectGuid().GetRawValue()), true);
    }
}

// "BOTGP" chunks of "A^<spell>^<acquired>^<name>" and "I^<item>^<tab C|R|Q>^<C continuous|D on demand>^<acquired>^<name>",
// separated by '~'
void GuildProgressSubsystem::SendList(Player* player)
{
    uint32 guildId = player->GetGuildId();
    std::vector<uint32> buffs = GetGuildBuffs(guildId);
    std::vector<uint32> items;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto it = guildItemsMap.find(guildId);
        if (it != guildItemsMap.end())
        {
            items = it->second;
        }
    }

    std::ostringstream payload;
    payload << "G^" << guildId;

    for (const AuraProgressSet& aura : approvedBuffs)
    {
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(aura.id);
        bool acquired = std::find(buffs.begin(), buffs.end(), aura.id) != buffs.end();
        payload << "~A^" << aura.id << "^" << (acquired ? 1 : 0) << "^" << Clean(spellInfo ? spellInfo->SpellName[0] : std::to_string(aura.id));
    }

    for (auto const& [list, code] : ItemCategories)
    {
        for (const ItemProgressSet& item : *list)
        {
            ItemPrototype const* proto = ObjectMgr::GetItemPrototype(item.id);
            bool acquired = std::find(items.begin(), items.end(), item.id) != items.end();
            payload << "~I^" << item.id << "^" << code << "^" << (item.acquireMethod == ItemAcquireMethod::Continuous ? 'C' : 'D') << "^"
                << (acquired ? 1 : 0) << "^" << Clean(proto ? proto->Name1 : std::to_string(item.id));
        }
    }

    std::string const text = payload.str();
    size_t const count = std::max<size_t>(1, (text.size() + CHUNK_SIZE - 1) / CHUNK_SIZE);
    uint32 const seq = ++m_chunkSeq % 256;
    for (size_t i = 0; i < count; ++i)
    {
        std::ostringstream chunk;
        chunk << "BOTGP\tS" << seq << ":" << (i + 1) << "/" << count << ":" << text.substr(i * CHUNK_SIZE, CHUNK_SIZE);

        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_PARTY, chunk.str().c_str(), LANG_ADDON, CHAT_TAG_NONE, player->GetObjectGuid(), player->GetName());
        player->GetSession()->SendPacket(data);
    }
}

std::vector<uint32> GuildProgressSubsystem::GetGuildBuffs(uint32 guildId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildBuffsMap.find(guildId);

    if (it == guildBuffsMap.end())
    {
        return { };
    }

    return it->second;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildItems(uint32 guildId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildReducedItemsMap.find(guildId);

    if (it == guildReducedItemsMap.end())
    {
        return { };
    }

    return it->second;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildBuffsForClass(uint32 guildId, uint8 classId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildBuffsMap.find(guildId);

    if (it == guildBuffsMap.end())
    {
        return { };
    }

    std::vector<uint32> buffs;
    buffs.reserve(it->second.size());

    for (uint32 spellId : it->second)
    {
        auto it = std::find_if(approvedBuffs.begin(), approvedBuffs.end(), [&](const AuraProgressSet& el) { return el.id == spellId; });

        if (it == approvedBuffs.end())
        {
            continue;
        }

        if (!IsEligible(it->eligible, classId))
        {
            continue;
        }

        buffs.emplace_back(spellId);
    }

    return buffs;
}

std::vector<uint32> GuildProgressSubsystem::GetGuildItemsForClass(uint32 guildId, uint8 classId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildReducedItemsMap.find(guildId);

    if (it == guildReducedItemsMap.end())
    {
        return { };
    }

    std::vector<uint32> items;
    items.reserve(it->second.size());

    for (uint32 itemId : it->second)
    {
        const ItemProgressSet* item = FindItem(itemId);

        if (item == nullptr)
        {
            continue;
        }

        if (!IsEligible(item->eligible, classId))
        {
            continue;
        }

        if (item->acquireMethod != ItemAcquireMethod::Continuous)
        {
            continue;
        }

        items.emplace_back(itemId);
    }

    return items;
}
