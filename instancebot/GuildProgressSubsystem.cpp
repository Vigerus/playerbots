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
#include "Spells/Spell.h"

#include "playerbot/ItemID.h"
#include "playerbot/PlayerbotAIConfig.h"

#include <algorithm>
#include <sstream>

#define ITEM_TYPE_ITEM 0
#define ITEM_TYPE_BUFF 1
#define ITEM_TYPE_ENCHANT 2

#define QUEST_FOR_THE_HORDE 4974                        // Horde: Rend's head, same rewards as Drakkisath's Demise
#define QUEST_GENERAL_DRAKKISATHS_DEMISE 5102           // Alliance
#define QUEST_IN_DREAMS 5944
#define QUEST_DRAKEFIRE_AMULET 6502                     // Alliance Onyxia attunement
#define QUEST_BLOOD_OF_THE_BLACK_DRAGON_CHAMPION 6602   // Horde Onyxia attunement
#define QUEST_CORRUPTION_OF_EARTH_AND_SEED_H 7064       // Princess Theradras
#define QUEST_CORRUPTION_OF_EARTH_AND_SEED_A 7065
#define QUEST_FOR_ALL_TO_SEE 7491                       // Horde: Onyxia's head
#define QUEST_CELEBRATING_GOOD_TIMES 7496               // Alliance: Onyxia's head
#define QUEST_LORD_OF_BLACKROCK_A 7782                  // Nefarian's head
#define QUEST_LORD_OF_BLACKROCK_H 7784
#define QUEST_SIGNETS_OF_THE_ZANDALAR 8246
#define QUEST_FALL_OF_OSSIRIAN 8791                     // Ossirian's head
#define QUEST_SAVIOR_OF_KALIMDOR 8802                   // C'Thun's eye

#define CHECK_INTERVAL 10       // seconds between two checks of a guild member's auras / items
#define CHUNK_SIZE 220

// who can use an item / aura, empty = everyone
static const std::vector<uint8> ALL = {};
static const std::vector<uint8> PHYSICAL = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN };
static const std::vector<uint8> STRENGTH = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN };
static const std::vector<uint8> WEAPON_USERS = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_SHAMAN };
static const std::vector<uint8> TANKS = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID };
static const std::vector<uint8> CASTERS = { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID };
static const std::vector<uint8> HEALERS = { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN };
static const std::vector<uint8> CASTERS_HEALERS = { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN };
static const std::vector<uint8> MANA_USERS = { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER };
static const std::vector<uint8> CLOTH = { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST };
static const std::vector<uint8> LEATHER = { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN };
static const std::vector<uint8> MAIL = { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN };
static const std::vector<uint8> PLATE = { CLASS_WARRIOR, CLASS_PALADIN };

static std::vector<ItemProgressSet> ConsumedItems = {
    // potions
    { ITEM_SWIFTNESS_POTION,                  Continuous, ALL },
    { ITEM_LIMITED_INVULNERABILITY_POTION,    Continuous, ALL },
    { ITEM_FREE_ACTION_POTION,                Continuous, ALL },
    { ITEM_LIVING_ACTION_POTION,              Continuous, ALL },
    { ITEM_RESTORATIVE_POTION,                Continuous, ALL },
    { ITEM_PURIFICATION_POTION,               Continuous, ALL },
    { ITEM_MAGIC_RESISTANCE_POTION,           Continuous, ALL },
    { ITEM_MAJOR_HEALING_POTION,              Continuous, ALL },
    { ITEM_MAJOR_REJUVENATION_POTION,         Continuous, ALL },
    { ITEM_MAJOR_TROLLS_BLOOD_POTION,         Continuous, ALL },
    { ITEM_SUPERIOR_MANA_POTION,              Continuous, CASTERS_HEALERS },
    { ITEM_MAJOR_MANA_POTION,                 Continuous, CASTERS_HEALERS },
    { ITEM_MAGEBLOOD_POTION,                  Continuous, CASTERS_HEALERS },
    { ITEM_GREAT_RAGE_POTION,                 Continuous, { CLASS_WARRIOR } },
    { ITEM_MIGHTY_RAGE_POTION,                Continuous, { CLASS_WARRIOR } },
    { ITEM_GREATER_STONESHIELD_POTION,        Continuous, TANKS },
    { ITEM_GREATER_DREAMLESS_SLEEP_POTION,    Continuous, HEALERS },
    { ITEM_DEMONIC_RUNE,                      Continuous, MANA_USERS },
    { ITEM_DARK_RUNE,                         Continuous, MANA_USERS },

    // protection potions
    { ITEM_SHADOW_PROTECTION_POTION,          Continuous, ALL },
    { ITEM_FIRE_PROTECTION_POTION,            Continuous, ALL },
    { ITEM_FROST_PROTECTION_POTION,           Continuous, ALL },
    { ITEM_NATURE_PROTECTION_POTION,          Continuous, ALL },
    { ITEM_GREATER_SHADOW_PROTECTION_POTION,  Continuous, ALL },
    { ITEM_GREATER_FIRE_PROTECTION_POTION,    Continuous, ALL },
    { ITEM_GREATER_FROST_PROTECTION_POTION,   Continuous, ALL },
    { ITEM_GREATER_NATURE_PROTECTION_POTION,  Continuous, ALL },
    { ITEM_GREATER_HOLY_PROTECTION_POTION,    Continuous, ALL },
    { ITEM_GREATER_ARCANE_PROTECTION_POTION,  Continuous, ALL },

    // elixirs
    { ITEM_ELIXIR_OF_FORTITUDE,               Continuous, ALL },
    { ITEM_GIFT_OF_ARTHAS,                    Continuous, TANKS },
    { ITEM_ELIXIR_OF_GIANTS,                  Continuous, TANKS },
    { ITEM_ELIXIR_OF_SUPERIOR_DEFENSE,        Continuous, { CLASS_WARRIOR, CLASS_DRUID } },
    { ITEM_ELIXIR_OF_GREATER_AGILITY,         Continuous, PHYSICAL },
    { ITEM_ELIXIR_OF_THE_MONGOOSE,            Continuous, PHYSICAL },
    { ITEM_ELIXIR_OF_BRUTE_FORCE,             Continuous, { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN } },
    { ITEM_ELIXIR_OF_THE_SAGES,               Continuous, MANA_USERS },
    { ITEM_GREATER_ARCANE_ELIXIR,             Continuous, CASTERS },
    { ITEM_ELIXIR_OF_SHADOW_POWER,            Continuous, { CLASS_WARLOCK, CLASS_PRIEST } },
    { ITEM_ELIXIR_OF_GREATER_FIREPOWER,       Continuous, { CLASS_MAGE, CLASS_WARLOCK } },
    { ITEM_ELIXIR_OF_FROST_POWER,             Continuous, { CLASS_MAGE } },

    // flasks
    { ITEM_FLASK_OF_THE_TITANS,               Continuous, ALL },
    { ITEM_FLASK_OF_CHROMATIC_RESISTANCE,     Continuous, ALL },
    { ITEM_FLASK_OF_PETRIFICATION,            Continuous, TANKS },
    { ITEM_FLASK_OF_DISTILLED_WISDOM,         Continuous, { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_HUNTER } },
    { ITEM_FLASK_OF_SUPREME_POWER,            Continuous, { CLASS_MAGE, CLASS_WARLOCK, CLASS_SHAMAN, CLASS_DRUID } },

    // Blasted Lands, Zul'Gurub
    { ITEM_ROIDS,                             Continuous, STRENGTH },
    { ITEM_GROUND_SCORPOK_ASSAY,              Continuous, PHYSICAL },
    { ITEM_LUNG_JUICE_COCKTAIL,               Continuous, ALL },
    { ITEM_CEREBRAL_CORTEX_COMPOUND,          Continuous, CASTERS },
    { ITEM_GIZZARD_GUM,                       Continuous, HEALERS },
    { ITEM_SPIRIT_OF_ZANZA,                   Continuous, ALL },
    { ITEM_SHEEN_OF_ZANZA,                    Continuous, ALL },
    { ITEM_SWIFTNESS_OF_ZANZA,                Continuous, ALL },

    // Winterspring, Felwood
    { ITEM_JUJU_POWER,                        Continuous, STRENGTH },
    { ITEM_JUJU_MIGHT,                        Continuous, PHYSICAL },
    { ITEM_JUJU_FLURRY,                       Continuous, PHYSICAL },
    { ITEM_JUJU_GUILE,                        Continuous, CASTERS },
    { ITEM_JUJU_EMBER,                        Continuous, ALL },
    { ITEM_JUJU_CHILL,                        Continuous, ALL },
    { ITEM_WINTERFALL_FIREWATER,              Continuous, PHYSICAL },
    { ITEM_WHIPPER_ROOT_TUBER,                Continuous, ALL },
    { ITEM_NIGHT_DRAGONS_BREATH,              Continuous, ALL },

    // food, drinks
    { ITEM_SMOKED_DESERT_DUMPLINGS,           Continuous, STRENGTH },
    { ITEM_GRILLED_SQUID,                     Continuous, PHYSICAL },
    { ITEM_RUNN_TUM_TUBER_SURPRISE,           Continuous, CASTERS },
    { ITEM_NIGHTFIN_SOUP,                     Continuous, MANA_USERS },
    { ITEM_SAGEFISH_DELIGHT,                  Continuous, MANA_USERS },
    { ITEM_TENDER_WOLF_STEAK,                 Continuous, ALL },
    { ITEM_DIRGES_KICKIN_CHIMAEROK_CHOPS,     Continuous, ALL },
    { ITEM_RUMSEY_RUM_BLACK_LABEL,            Continuous, ALL },

    // mage conjured: a member gets only the best rank of each (ConjuredRanks)
    { ITEM_CONJURED_WATER,                    Continuous, MANA_USERS },
    { ITEM_CONJURED_FRESH_WATER,              Continuous, MANA_USERS },
    { ITEM_CONJURED_PURIFIED_WATER,           Continuous, MANA_USERS },
    { ITEM_CONJURED_SPRING_WATER,             Continuous, MANA_USERS },
    { ITEM_CONJURED_MINERAL_WATER,            Continuous, MANA_USERS },
    { ITEM_CONJURED_SPARKLING_WATER,          Continuous, MANA_USERS },
    { ITEM_CONJURED_CRYSTAL_WATER,            Continuous, MANA_USERS },
    { ITEM_CONJURED_MUFFIN,                   Continuous, ALL },
    { ITEM_CONJURED_BREAD,                    Continuous, ALL },
    { ITEM_CONJURED_RYE,                      Continuous, ALL },
    { ITEM_CONJURED_PUMPERNICKEL,             Continuous, ALL },
    { ITEM_CONJURED_SOURDOUGH,                Continuous, ALL },
    { ITEM_CONJURED_SWEET_ROLL,               Continuous, ALL },
    { ITEM_CONJURED_CINNAMON_ROLL,            Continuous, ALL },

    // weapon stones, oils
    { ITEM_DENSE_WEIGHTSTONE,                 Continuous, WEAPON_USERS },
    { ITEM_ELEMENTAL_SHARPENING_STONE,        Continuous, WEAPON_USERS },
    { ITEM_BRILLIANT_MANA_OIL,                Continuous, CASTERS_HEALERS },
    { ITEM_BRILLIANT_WIZARD_OIL,              Continuous, CASTERS },

    // other
    { ITEM_HEAVY_RUNECLOTH_BANDAGE,           Continuous, ALL },
    { ITEM_AQUAL_QUINTESSENCE,                Continuous, ALL }
};

static std::vector<ItemProgressSet> CraftedItems = {
    // blacksmithing
    { ITEM_STRONGHOLD_GAUNTLETS,              OnDemand,   PLATE },
    { ITEM_LIONHEART_HELM,                    OnDemand,   PLATE },
    { ITEM_DARK_IRON_LEGGINGS,                OnDemand,   PLATE },
    { ITEM_DARK_IRON_BRACERS,                 OnDemand,   PLATE },
    { ITEM_ARCANITE_REAPER,                   OnDemand,   MAIL },
    { ITEM_GIRDLE_OF_THE_DAWN,                OnDemand,   ALL },

    // leatherworking
    { ITEM_BLACK_DRAGONSCALE_BREASTPLATE,     OnDemand,   MAIL },
    { ITEM_BLACK_DRAGONSCALE_SHOULDERS,       OnDemand,   MAIL },
    { ITEM_BLACK_DRAGONSCALE_LEGGINGS,        OnDemand,   MAIL },
    { ITEM_BLACK_DRAGONSCALE_BOOTS,           OnDemand,   MAIL },
    { ITEM_DEVILSAUR_LEGGINGS,                OnDemand,   LEATHER },
    { ITEM_DEVILSAUR_GAUNTLETS,               OnDemand,   LEATHER },
    { ITEM_PRIMAL_BATSKIN_JERKIN,             OnDemand,   LEATHER },
    { ITEM_PRIMAL_BATSKIN_GLOVES,             OnDemand,   LEATHER },
    { ITEM_PRIMAL_BATSKIN_BRACERS,            OnDemand,   LEATHER },
    { ITEM_BLOOD_TIGER_BREASTPLATE,           OnDemand,   LEATHER },
    { ITEM_BLOOD_TIGER_SHOULDERS,             OnDemand,   LEATHER },
    { ITEM_SHIFTING_CLOAK,                    OnDemand,   { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID } },
    { ITEM_HIDE_OF_THE_WILD,                  OnDemand,   HEALERS },
    { ITEM_CHROMATIC_CLOAK,                   OnDemand,   { CLASS_MAGE, CLASS_WARLOCK } },

    // tailoring
    { ITEM_ROBE_OF_THE_ARCHMAGE,              OnDemand,   { CLASS_MAGE } },
    { ITEM_ROBE_OF_THE_VOID,                  OnDemand,   { CLASS_WARLOCK } },
    { ITEM_TRUEFAITH_VESTMENTS,               OnDemand,   { CLASS_PRIEST } },
    { ITEM_GLOVES_OF_SPELL_MASTERY,           OnDemand,   { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID } },
    { ITEM_INFERNO_GLOVES,                    OnDemand,   { CLASS_MAGE, CLASS_WARLOCK } },
    { ITEM_FLARECORE_MANTLE,                  OnDemand,   CASTERS },
    { ITEM_FLARECORE_WRAPS,                   OnDemand,   CASTERS },
    { ITEM_FLARECORE_ROBE,                    OnDemand,   CASTERS },
    { ITEM_FLARECORE_LEGGINGS,                OnDemand,   CASTERS },
    { ITEM_MOONCLOTH_ROBE,                    OnDemand,   HEALERS },
    { ITEM_BLOODVINE_VEST,                    OnDemand,   CLOTH },
    { ITEM_BLOODVINE_LEGGINGS,                OnDemand,   CLOTH },
    { ITEM_BLOODVINE_BOOTS,                   OnDemand,   CLOTH }
};

// every reward choice of a quest unlocks for the guild when one member rewards it, whichever one was picked
static std::vector<ItemProgressSet> QuestItems = {
    // Signets of the Zandalar (Zul'Gurub)
    { ITEM_ZANDALAR_SIGNET_OF_MOJO,           OnDemand,   CASTERS, { QUEST_SIGNETS_OF_THE_ZANDALAR } },
    { ITEM_ZANDALAR_SIGNET_OF_MIGHT,          OnDemand,   PHYSICAL, { QUEST_SIGNETS_OF_THE_ZANDALAR } },
    { ITEM_ZANDALAR_SIGNET_OF_SERENITY,       OnDemand,   HEALERS, { QUEST_SIGNETS_OF_THE_ZANDALAR } },

    // In Dreams (Western Plaguelands)
    { ITEM_MARK_OF_FORDRING,                  OnDemand,   PHYSICAL, { QUEST_IN_DREAMS } },
    { ITEM_ORNATE_ADAMANTIUM_BREASTPLATE,     OnDemand,   PLATE, { QUEST_IN_DREAMS } },
    { ITEM_SHIMMERING_PLATINUM_WARHAMMER,     OnDemand,   ALL, { QUEST_IN_DREAMS } },
    { ITEM_SHROUD_OF_THE_EXILE,               OnDemand,   ALL, { QUEST_IN_DREAMS } },
    { ITEM_FORDRINGS_SEAL,                    OnDemand,   ALL, { QUEST_IN_DREAMS } },

    // General Drakkisath's Demise / For The Horde! (Upper Blackrock Spire)
    { ITEM_BLACKHANDS_BREADTH,                OnDemand,   PHYSICAL, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE } },
    { ITEM_MARK_OF_TYRANNY,                   OnDemand,   ALL, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE } },
    { ITEM_EYE_OF_THE_BEAST,                  OnDemand,   CASTERS, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE } },

    // Onyxia attunement
    { ITEM_DRAKEFIRE_AMULET,                  OnDemand,   ALL, { QUEST_DRAKEFIRE_AMULET, QUEST_BLOOD_OF_THE_BLACK_DRAGON_CHAMPION } },

    // Corruption of Earth and Seed (Maraudon)
    { ITEM_THRASH_BLADE,                      OnDemand,   { CLASS_WARRIOR, CLASS_ROGUE, CLASS_PALADIN, CLASS_HUNTER }, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A } },
    { ITEM_RESURGENCE_ROD,                    OnDemand,   CASTERS_HEALERS, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A } },
    { ITEM_VERDANT_KEEPERS_AIM,               OnDemand,   { CLASS_WARRIOR, CLASS_ROGUE, CLASS_HUNTER }, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A } },

    // Onyxia's head
    { ITEM_DRAGONSLAYERS_SIGNET,              OnDemand,   CASTERS, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES } },
    { ITEM_ONYXIA_TOOTH_PENDANT,              OnDemand,   PHYSICAL, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES } },
    { ITEM_ONYXIA_BLOOD_TALISMAN,             OnDemand,   HEALERS, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES } },

    // Nefarian's head (The Lord of Blackrock)
    { ITEM_MASTER_DRAGONSLAYERS_ORB,          OnDemand,   CASTERS, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A } },
    { ITEM_MASTER_DRAGONSLAYERS_MEDALLION,    OnDemand,   TANKS, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A } },
    { ITEM_MASTER_DRAGONSLAYERS_RING,         OnDemand,   PHYSICAL, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A } },

    // Ossirian's head (The Fall of Ossirian)
    { ITEM_CHARM_OF_THE_SHIFTING_SANDS,       OnDemand,   ALL, { QUEST_FALL_OF_OSSIRIAN } },
    { ITEM_CHOKER_OF_THE_SHIFTING_SANDS,      OnDemand,   ALL, { QUEST_FALL_OF_OSSIRIAN } },
    { ITEM_PENDANT_OF_THE_SHIFTING_SANDS,     OnDemand,   ALL, { QUEST_FALL_OF_OSSIRIAN } },
    { ITEM_AMULET_OF_THE_SHIFTING_SANDS,      OnDemand,   ALL, { QUEST_FALL_OF_OSSIRIAN } },

    // C'Thun's eye (The Savior of Kalimdor)
    { ITEM_RING_OF_THE_FALLEN_GOD,            OnDemand,   ALL, { QUEST_SAVIOR_OF_KALIMDOR } },
    { ITEM_CLOAK_OF_THE_FALLEN_GOD,           OnDemand,   ALL, { QUEST_SAVIOR_OF_KALIMDOR } },
    { ITEM_AMULET_OF_THE_FALLEN_GOD,          OnDemand,   ALL, { QUEST_SAVIOR_OF_KALIMDOR } }
};

// mage conjured food / water, lowest rank first, with the level the rank needs to be eaten / drunk
struct ConjuredRank
{
    uint32 itemId;
    uint32 level;
};

static const std::vector<std::vector<ConjuredRank>> ConjuredRanks = {
    {
        { ITEM_CONJURED_WATER, 1 },
        { ITEM_CONJURED_FRESH_WATER, 5 },
        { ITEM_CONJURED_PURIFIED_WATER, 15 },
        { ITEM_CONJURED_SPRING_WATER, 25 },
        { ITEM_CONJURED_MINERAL_WATER, 35 },
        { ITEM_CONJURED_SPARKLING_WATER, 45 },
        { ITEM_CONJURED_CRYSTAL_WATER, 55 }
    },
    {
        { ITEM_CONJURED_MUFFIN, 1 },
        { ITEM_CONJURED_BREAD, 5 },
        { ITEM_CONJURED_RYE, 15 },
        { ITEM_CONJURED_PUMPERNICKEL, 25 },
        { ITEM_CONJURED_SOURDOUGH, 35 },
        { ITEM_CONJURED_SWEET_ROLL, 45 },
        { ITEM_CONJURED_CINNAMON_ROLL, 55 }
    }
};

// addon tab codes: C consumables, R crafted, Q quest
static const std::vector<std::pair<const std::vector<ItemProgressSet>*, char>> ItemCategories = {
    { &ConsumedItems, 'C' }, { &CraftedItems, 'R' }, { &QuestItems, 'Q' }
};

static std::vector<AuraProgressSet> approvedBuffs = {
    { 10767, CASTERS_HEALERS },                     // Rising Spirit (Agamaggan)
    { 15123, ALL },                                 // Resist Fire
    { 15366, ALL },                                 // Songflower Serenade
    { 16609, ALL },                                 // Warchief's Blessing
    { 16610, { CLASS_WARRIOR, CLASS_DRUID } },      // Razorhide (Agamaggan)
    { 16612, PHYSICAL },                            // Agamaggan's Strength (Agamaggan)
    { 16618, ALL },                                 // Spirit of the Wind (Agamaggan)
    { 17013, PHYSICAL },                            // Agamaggan's Agility (Agamaggan)
    { 22888, ALL },                                 // Rallying Cry of the Dragonslayer
    { 24425, ALL }                                  // Spirit of Zandalar
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

// a conjured food / water rank is given only when it is the highest unlocked rank the player's level can use
static bool IsBestConjuredRank(uint32 itemId, const std::vector<uint32>& unlocked, uint32 level)
{
    for (const std::vector<ConjuredRank>& ranks : ConjuredRanks)
    {
        if (std::none_of(ranks.begin(), ranks.end(), [&](const ConjuredRank& rank) { return rank.itemId == itemId; }))
        {
            continue;
        }

        for (auto rank = ranks.rbegin(); rank != ranks.rend(); ++rank)
        {
            if (rank->level <= level && std::find(unlocked.begin(), unlocked.end(), rank->itemId) != unlocked.end())
            {
                return rank->itemId == itemId;
            }
        }

        return false;
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

    GlobalEventSubsystem::instance().GetOnItemEnchantedDelegate().Add([&](Player* player, uint32 spellId)
    {
        OnItemEnchanted(player, spellId);
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
            else if (item_type == ITEM_TYPE_ENCHANT)
            {
                guildEnchantsMap[guild_id].push_back(item_id);
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
        if (std::find(item.questIds.begin(), item.questIds.end(), questId) != item.questIds.end() && UnlockItem(guildId, item.id))
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

void GuildProgressSubsystem::OnItemEnchanted(Player* player, uint32 spellId)
{
    if (player == nullptr)
    {
        return;
    }

    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    uint32 guildId = player->GetGuildId();
    Guild* guild = sGuildMgr.GetGuildById(guildId);

    if (spellInfo == nullptr || guild == nullptr)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_lock);
        std::vector<uint32>& guildEnchants = guildEnchantsMap[guildId];

        if (std::find(guildEnchants.begin(), guildEnchants.end(), spellId) != guildEnchants.end())
        {
            return;
        }

        guildEnchants.push_back(spellId);
    }

    std::ostringstream ss;
    ss << "New enchant " << spellInfo->SpellName[0] << " added to the guild pool!";
    Save(guildId, ITEM_TYPE_ENCHANT, spellId);
    guild->BroadcastEvent(GE_MOTD, ss.str().c_str());
}

bool GuildProgressSubsystem::IsEnchantUnlocked(uint32 guildId, uint32 spellId) const
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = guildEnchantsMap.find(guildId);
    return it != guildEnchantsMap.end() && std::find(it->second.begin(), it->second.end(), spellId) != it->second.end();
}

void GuildProgressSubsystem::ApplyGuildProgress(Player* player)
{
    uint32 guildId = player->GetGuildId();
    uint8 classId = player->getClass();

    // quests rewarded before the guild tracked them
    for (const ItemProgressSet& item : QuestItems)
    {
        if (IsItemUnlocked(guildId, item.id))
        {
            continue;
        }

        for (uint32 questId : item.questIds)
        {
            if (player->GetQuestRewardStatus(questId))
            {
                UnlockQuest(guildId, questId);
            }
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

    std::vector<uint32> items = GetGuildItemsForClass(guildId, classId);
    for (uint32 itemId : items)
    {
        if (!IsBestConjuredRank(itemId, items, player->GetLevel()))
        {
            continue;
        }

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
        return;
    }

    if (command == "enchant")
    {
        EnchantItem(player, in);
    }
}

// "guildprogress enchant <spell> <bag> <slot>": the client refuses a recipe without its reagents on its own, so the
// server casts it; <bag> -1 is an equipped item (<slot> 1-19), 0 the backpack, 1-4 the bags, as in the client's API
void GuildProgressSubsystem::EnchantItem(Player* player, std::istream& in)
{
    uint32 spellId = 0;
    int32 bag = 0;
    int32 slot = 0;
    in >> spellId >> bag >> slot;

    SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
    if (spellInfo == nullptr || !player->HasActiveSpell(spellId) || !IsEnchantUnlocked(player->GetGuildId(), spellId))
    {
        ChatHandler(player).SendSysMessage("Your guild has not unlocked that enchant.");
        return;
    }

    Item* item = nullptr;
    if (bag == -1 && slot >= 1 && slot <= EQUIPMENT_SLOT_END)
    {
        item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot - 1);
    }
    else if (bag == 0 && slot >= 1)
    {
        item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, INVENTORY_SLOT_ITEM_START + slot - 1);
    }
    else if (bag >= 1 && bag <= 4 && slot >= 1)
    {
        item = player->GetItemByPos(INVENTORY_SLOT_BAG_START + bag - 1, slot - 1);
    }

    if (item == nullptr)
    {
        ChatHandler(player).SendSysMessage("No item there to enchant.");
        return;
    }

    // a normal cast: the cast bar, the tools, the item's class; only the reagents are skipped
    SpellCastTargets targets;
    targets.setItemTarget(item);
    Spell* spell = new Spell(player, spellInfo, TRIGGERED_NONE);
    spell->SpellStart(&targets);
}

// "BOTGP" chunks of "A^<spell>^<acquired>^<name>", "I^<item>^<tab C|R|Q>^<C continuous|D on demand>^<acquired>^<name>"
// and "E^<spell>^<name>" (the guild's enchants), separated by '~'
void GuildProgressSubsystem::SendList(Player* player)
{
    uint32 guildId = player->GetGuildId();
    std::vector<uint32> buffs = GetGuildBuffs(guildId);
    std::vector<uint32> items;
    std::vector<uint32> enchants;
    {
        std::lock_guard<std::mutex> lock(m_lock);
        auto it = guildItemsMap.find(guildId);
        if (it != guildItemsMap.end())
        {
            items = it->second;
        }
        auto enchantIt = guildEnchantsMap.find(guildId);
        if (enchantIt != guildEnchantsMap.end())
        {
            enchants = enchantIt->second;
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

    for (uint32 spellId : enchants)
    {
        SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        payload << "~E^" << spellId << "^" << Clean(spellInfo ? spellInfo->SpellName[0] : std::to_string(spellId));
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
