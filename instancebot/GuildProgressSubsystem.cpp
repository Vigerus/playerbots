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

static std::vector<ItemProgressSet> ConsumedItems = {
    {ITEM_SWIFTNESS_POTION, ItemAcquireMethod::Continuous, // # 2459 - Swiftness Potion
    {}},
    {ITEM_LIMITED_INVULNERABILITY_POTION, ItemAcquireMethod::Continuous, // # 3387 - Limited Invulnerability Potion
    {}},
    {ITEM_ELIXIR_OF_FORTITUDE, ItemAcquireMethod::Continuous, // # 3825 - Elixir of Fortitude
    {}},
    {ITEM_GREAT_RAGE_POTION, ItemAcquireMethod::Continuous, // # 5633 - Great Rage Potion
    { CLASS_WARRIOR }},
    {ITEM_FREE_ACTION_POTION, ItemAcquireMethod::Continuous, // # 5634 - Free Action Potion
    {}},
    {ITEM_SHADOW_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 6048 - Shadow Protection Potion
    {}},
    {ITEM_FIRE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 6049 - Fire Protection Potion
    {}},
    {ITEM_FROST_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 6050 - Frost Protection Potion
    {}},
    {ITEM_NATURE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 6052 - Nature Protection Potion
    {}},
    {ITEM_ROIDS, ItemAcquireMethod::Continuous, // # 8410 - R.O.I.D.S.
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN, CLASS_ROGUE }},
    {ITEM_LUNG_JUICE_COCKTAIL, ItemAcquireMethod::Continuous, // # 8411 - Lung Juice Cocktail
    {}},
    {ITEM_GROUND_SCORPOK_ASSAY, ItemAcquireMethod::Continuous, // # 8412 - Ground Scorpok Assay
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_CEREBRAL_CORTEX_COMPOUND, ItemAcquireMethod::Continuous, // # 8423 - Cerebral Cortex Compound
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_GIZZARD_GUM, ItemAcquireMethod::Continuous, // # 8424 - Gizzard Gum
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_RESTORATIVE_POTION, ItemAcquireMethod::Continuous, // # 9030 - Restorative Potion
    {}},
    {ITEM_MAGIC_RESISTANCE_POTION, ItemAcquireMethod::Continuous, // # 9036 - Magic Resistance Potion
    {}},
    {ITEM_GIFT_OF_ARTHAS, ItemAcquireMethod::Continuous, // # 9088 - Gift of Arthas
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},
    {ITEM_ELIXIR_OF_GREATER_AGILITY, ItemAcquireMethod::Continuous, // # 9187 - Elixir of Greater Agility
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_ELIXIR_OF_GIANTS, ItemAcquireMethod::Continuous, // # 9206 - Elixir of Giants
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},
    {ITEM_ELIXIR_OF_SHADOW_POWER, ItemAcquireMethod::Continuous, // # 9264 - Elixir of Shadow Power
    { CLASS_WARLOCK, CLASS_PRIEST }},
    {ITEM_WHIPPER_ROOT_TUBER, ItemAcquireMethod::Continuous, // # 11951 - Whipper Root Tuber
    {}},
    {ITEM_NIGHT_DRAGONS_BREATH, ItemAcquireMethod::Continuous, // # 11952 - Night Dragon's Breath
    {}},
    {ITEM_JUJU_FLURRY, ItemAcquireMethod::Continuous, // # 12450 - Juju Flurry
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_JUJU_POWER, ItemAcquireMethod::Continuous, // # 12451 - Juju Power
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN, CLASS_ROGUE }},
    {ITEM_JUJU_EMBER, ItemAcquireMethod::Continuous, // # 12455 - Juju Ember
    {}},
    {ITEM_JUJU_CHILL, ItemAcquireMethod::Continuous, // # 12457 - Juju Chill
    {}},
    {ITEM_JUJU_GUILE, ItemAcquireMethod::Continuous, // # 12458 - Juju Guile
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_JUJU_MIGHT, ItemAcquireMethod::Continuous, // # 12460 - Juju Might
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_DENSE_WEIGHTSTONE, ItemAcquireMethod::Continuous, // # 12643 - Dense Weightstone
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_SHAMAN }},
    {ITEM_DEMONIC_RUNE, ItemAcquireMethod::Continuous, // # 12662 - Demonic Rune
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER }},
    {ITEM_WINTERFALL_FIREWATER, ItemAcquireMethod::Continuous, // # 12820 - Winterfall Firewater
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_MIGHTY_RAGE_POTION, ItemAcquireMethod::Continuous, // # 13442 - Mighty Rage Potion
    { CLASS_WARRIOR }},
    {ITEM_SUPERIOR_MANA_POTION, ItemAcquireMethod::Continuous, // # 13443 - Superior Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_MAJOR_MANA_POTION, ItemAcquireMethod::Continuous, // # 13444 - Major Mana Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_ELIXIR_OF_SUPERIOR_DEFENSE, ItemAcquireMethod::Continuous, // # 13445 - Elixir of Superior Defense
    { CLASS_WARRIOR, CLASS_DRUID }},
    {ITEM_MAJOR_HEALING_POTION, ItemAcquireMethod::Continuous, // # 13446 - Major Healing Potion
    {}},
    {ITEM_ELIXIR_OF_THE_SAGES, ItemAcquireMethod::Continuous, // # 13447 - Elixir of the Sages
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER }},
    {ITEM_ELIXIR_OF_THE_MONGOOSE, ItemAcquireMethod::Continuous, // # 13452 - Elixir of the Mongoose
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_ELIXIR_OF_BRUTE_FORCE, ItemAcquireMethod::Continuous, // # 13453 - Elixir of Brute Force
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_GREATER_ARCANE_ELIXIR, ItemAcquireMethod::Continuous, // # 13454 - Greater Arcane Elixir
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_GREATER_STONESHIELD_POTION, ItemAcquireMethod::Continuous, // # 13455 - Greater Stoneshield Potion
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},
    {ITEM_GREATER_FROST_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13456 - Greater Frost Protection Potion
    {}},
    {ITEM_GREATER_FIRE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13457 - Greater Fire Protection Potion
    {}},
    {ITEM_GREATER_NATURE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13458 - Greater Nature Protection Potion
    {}},
    {ITEM_GREATER_SHADOW_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13459 - Greater Shadow Protection Potion
    {}},
    {ITEM_GREATER_HOLY_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13460 - Greater Holy Protection Potion
    {}},
    {ITEM_GREATER_ARCANE_PROTECTION_POTION, ItemAcquireMethod::Continuous, // # 13461 - Greater Arcane Protection Potion
    {}},
    {ITEM_PURIFICATION_POTION, ItemAcquireMethod::Continuous, // # 13462 - Purification Potion
    {}},
    {ITEM_FLASK_OF_PETRIFICATION, ItemAcquireMethod::Continuous, // # 13506 - Flask of Petrification
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }},
    {ITEM_FLASK_OF_THE_TITANS, ItemAcquireMethod::Continuous, // # 13510 - Flask of the Titans
    {}},
    {ITEM_FLASK_OF_DISTILLED_WISDOM, ItemAcquireMethod::Continuous, // # 13511 - Flask of Distilled Wisdom
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_HUNTER }},
    {ITEM_FLASK_OF_SUPREME_POWER, ItemAcquireMethod::Continuous, // # 13512 - Flask of Supreme Power
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_FLASK_OF_CHROMATIC_RESISTANCE, ItemAcquireMethod::Continuous, // # 13513 - Flask of Chromatic Resistance
    {}},
    {ITEM_GRILLED_SQUID, ItemAcquireMethod::Continuous, // # 13928 - Grilled Squid
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_NIGHTFIN_SOUP, ItemAcquireMethod::Continuous, // # 13931 - Nightfin Soup
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER }},
    {ITEM_HEAVY_RUNECLOTH_BANDAGE, ItemAcquireMethod::Continuous, // # 14530 - Heavy Runecloth Bandage
    {}},
    {ITEM_AQUAL_QUINTESSENCE, ItemAcquireMethod::Continuous, // # 17333 - Aqual Quintessence
    {}},
    {ITEM_ELIXIR_OF_FROST_POWER, ItemAcquireMethod::Continuous, // # 17708 - Elixir of Frost Power
    { CLASS_MAGE }},
    {ITEM_TENDER_WOLF_STEAK, ItemAcquireMethod::Continuous, // # 18045 - Tender Wolf Steak
    {}},
    {ITEM_MAJOR_REJUVENATION_POTION, ItemAcquireMethod::Continuous, // # 18253 - Major Rejuvenation Potion
    {}},
    {ITEM_RUNN_TUM_TUBER_SURPRISE, ItemAcquireMethod::Continuous, // # 18254 - Runn Tum Tuber Surprise
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_ELEMENTAL_SHARPENING_STONE, ItemAcquireMethod::Continuous, // # 18262 - Elemental Sharpening Stone
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_SHAMAN }},
    {ITEM_GREATER_DREAMLESS_SLEEP_POTION, ItemAcquireMethod::Continuous, // # 20002 - Greater Dreamless Sleep Potion
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_MAJOR_TROLLS_BLOOD_POTION, ItemAcquireMethod::Continuous, // # 20004 - Major Troll's Blood Potion
    {}},
    {ITEM_MAGEBLOOD_POTION, ItemAcquireMethod::Continuous, // # 20007 - Mageblood Potion
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_LIVING_ACTION_POTION, ItemAcquireMethod::Continuous, // # 20008 - Living Action Potion
    {}},
    {ITEM_SPIRIT_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20079 - Spirit of Zanza
    {}},
    {ITEM_SHEEN_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20080 - Sheen of Zanza
    {}},
    {ITEM_SWIFTNESS_OF_ZANZA, ItemAcquireMethod::Continuous, // # 20081 - Swiftness of Zanza
    {}},
    {ITEM_SMOKED_DESERT_DUMPLINGS, ItemAcquireMethod::Continuous, // # 20452 - Smoked Desert Dumplings
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID, CLASS_SHAMAN, CLASS_ROGUE }},
    {ITEM_DARK_RUNE, ItemAcquireMethod::Continuous, // # 20520 - Dark Rune
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER }},
    {ITEM_BRILLIANT_MANA_OIL, ItemAcquireMethod::Continuous, // # 20748 - Brilliant Mana Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PALADIN, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_BRILLIANT_WIZARD_OIL, ItemAcquireMethod::Continuous, // # 20749 - Brilliant Wizard Oil
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }},
    {ITEM_DIRGES_KICKIN_CHIMAEROK_CHOPS, ItemAcquireMethod::Continuous, // # 21023 - Dirge's Kickin' Chimaerok Chops
    {}},
    {ITEM_RUMSEY_RUM_BLACK_LABEL, ItemAcquireMethod::Continuous, // # 21151 - Rumsey Rum Black Label
    {}},
    {ITEM_SAGEFISH_DELIGHT, ItemAcquireMethod::Continuous, // # 21217 - Sagefish Delight
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_HUNTER }},
    {ITEM_ELIXIR_OF_GREATER_FIREPOWER, ItemAcquireMethod::Continuous, // # 21546 - Elixir of Greater Firepower
    { CLASS_MAGE, CLASS_WARLOCK }}
};

static std::vector<ItemProgressSet> CraftedItems = {
    {ITEM_STRONGHOLD_GAUNTLETS, ItemAcquireMethod::OnDemand, // # 12639 - Stronghold Gauntlets
    { CLASS_WARRIOR, CLASS_PALADIN }},
    {ITEM_LIONHEART_HELM, ItemAcquireMethod::OnDemand, // # 12640 - Lionheart Helm
    { CLASS_WARRIOR, CLASS_PALADIN }},
    {ITEM_ARCANITE_REAPER, ItemAcquireMethod::OnDemand, // # 12784 - Arcanite Reaper
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN }},
    {ITEM_GLOVES_OF_SPELL_MASTERY, ItemAcquireMethod::OnDemand, // # 14146 - Gloves of Spell Mastery
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID }},
    {ITEM_ROBE_OF_THE_ARCHMAGE, ItemAcquireMethod::OnDemand, // # 14152 - Robe of the Archmage
    { CLASS_MAGE }},
    {ITEM_ROBE_OF_THE_VOID, ItemAcquireMethod::OnDemand, // # 14153 - Robe of the Void
    { CLASS_WARLOCK }},
    {ITEM_TRUEFAITH_VESTMENTS, ItemAcquireMethod::OnDemand, // # 14154 - Truefaith Vestments
    { CLASS_PRIEST }},
    {ITEM_BLACK_DRAGONSCALE_BREASTPLATE, ItemAcquireMethod::OnDemand, // # 15050 - Black Dragonscale Breastplate
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN }},
    {ITEM_BLACK_DRAGONSCALE_SHOULDERS, ItemAcquireMethod::OnDemand, // # 15051 - Black Dragonscale Shoulders
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN }},
    {ITEM_BLACK_DRAGONSCALE_LEGGINGS, ItemAcquireMethod::OnDemand, // # 15052 - Black Dragonscale Leggings
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN }},
    {ITEM_DEVILSAUR_LEGGINGS, ItemAcquireMethod::OnDemand, // # 15062 - Devilsaur Leggings
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_DEVILSAUR_GAUNTLETS, ItemAcquireMethod::OnDemand, // # 15063 - Devilsaur Gauntlets
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_FLARECORE_MANTLE, ItemAcquireMethod::OnDemand, // # 16980 - Flarecore Mantle
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_BLACK_DRAGONSCALE_BOOTS, ItemAcquireMethod::OnDemand, // # 16984 - Black Dragonscale Boots
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_SHAMAN }},
    {ITEM_DARK_IRON_LEGGINGS, ItemAcquireMethod::OnDemand, // # 17013 - Dark Iron Leggings
    { CLASS_WARRIOR, CLASS_PALADIN }},
    {ITEM_DARK_IRON_BRACERS, ItemAcquireMethod::OnDemand, // # 17014 - Dark Iron Bracers
    { CLASS_WARRIOR, CLASS_PALADIN }},
    {ITEM_FLARECORE_WRAPS, ItemAcquireMethod::OnDemand, // # 18263 - Flarecore Wraps
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_INFERNO_GLOVES, ItemAcquireMethod::OnDemand, // # 18408 - Inferno Gloves
    { CLASS_MAGE, CLASS_WARLOCK }},
    {ITEM_MOONCLOTH_ROBE, ItemAcquireMethod::OnDemand, // # 18486 - Mooncloth Robe
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_CHROMATIC_CLOAK, ItemAcquireMethod::OnDemand, // # 18509 - Chromatic Cloak
    { CLASS_MAGE, CLASS_WARLOCK }},
    {ITEM_HIDE_OF_THE_WILD, ItemAcquireMethod::OnDemand, // # 18510 - Hide of the Wild
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }},
    {ITEM_SHIFTING_CLOAK, ItemAcquireMethod::OnDemand, // # 18511 - Shifting Cloak
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID }},
    {ITEM_GIRDLE_OF_THE_DAWN, ItemAcquireMethod::OnDemand, // # 19051 - Girdle of the Dawn
    {}},
    {ITEM_FLARECORE_ROBE, ItemAcquireMethod::OnDemand, // # 19156 - Flarecore Robe
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_FLARECORE_LEGGINGS, ItemAcquireMethod::OnDemand, // # 19165 - Flarecore Leggings
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_BLOODVINE_VEST, ItemAcquireMethod::OnDemand, // # 19682 - Bloodvine Vest
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST }},
    {ITEM_BLOODVINE_LEGGINGS, ItemAcquireMethod::OnDemand, // # 19683 - Bloodvine Leggings
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST }},
    {ITEM_BLOODVINE_BOOTS, ItemAcquireMethod::OnDemand, // # 19684 - Bloodvine Boots
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST }},
    {ITEM_PRIMAL_BATSKIN_JERKIN, ItemAcquireMethod::OnDemand, // # 19685 - Primal Batskin Jerkin
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_PRIMAL_BATSKIN_GLOVES, ItemAcquireMethod::OnDemand, // # 19686 - Primal Batskin Gloves
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_PRIMAL_BATSKIN_BRACERS, ItemAcquireMethod::OnDemand, // # 19687 - Primal Batskin Bracers
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_BLOOD_TIGER_BREASTPLATE, ItemAcquireMethod::OnDemand, // # 19688 - Blood Tiger Breastplate
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }},
    {ITEM_BLOOD_TIGER_SHOULDERS, ItemAcquireMethod::OnDemand, // # 19689 - Blood Tiger Shoulders
    { CLASS_WARRIOR, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }}
};

// every reward choice of a quest unlocks for the guild when one member rewards it, whichever one was picked
static std::vector<ItemProgressSet> QuestItems = {
// Signets of the Zandalar (Zul'Gurub)
    {ITEM_ZANDALAR_SIGNET_OF_MOJO, ItemAcquireMethod::OnDemand, // # 20076 - Zandalar Signet of Mojo
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, { QUEST_SIGNETS_OF_THE_ZANDALAR }},
    {ITEM_ZANDALAR_SIGNET_OF_MIGHT, ItemAcquireMethod::OnDemand, // # 20077 - Zandalar Signet of Might
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, { QUEST_SIGNETS_OF_THE_ZANDALAR }},
    {ITEM_ZANDALAR_SIGNET_OF_SERENITY, ItemAcquireMethod::OnDemand, // # 20078 - Zandalar Signet of Serenity
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }, { QUEST_SIGNETS_OF_THE_ZANDALAR }},

// In Dreams (Western Plaguelands)
    {ITEM_MARK_OF_FORDRING, ItemAcquireMethod::OnDemand, // # 15411 - Mark of Fordring
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, { QUEST_IN_DREAMS }},
    {ITEM_ORNATE_ADAMANTIUM_BREASTPLATE, ItemAcquireMethod::OnDemand, // # 15413 - Ornate Adamantium Breastplate
    { CLASS_WARRIOR, CLASS_PALADIN }, { QUEST_IN_DREAMS }},
    {ITEM_SHIMMERING_PLATINUM_WARHAMMER, ItemAcquireMethod::OnDemand, // # 15418 - Shimmering Platinum Warhammer
    {}, { QUEST_IN_DREAMS }},
    {ITEM_SHROUD_OF_THE_EXILE, ItemAcquireMethod::OnDemand, // # 15421 - Shroud of the Exile
    {}, { QUEST_IN_DREAMS }},
    {ITEM_FORDRINGS_SEAL, ItemAcquireMethod::OnDemand, // # 16058 - Fordring's Seal
    {}, { QUEST_IN_DREAMS }},

// General Drakkisath's Demise / For The Horde! (Upper Blackrock Spire)
    {ITEM_BLACKHANDS_BREADTH, ItemAcquireMethod::OnDemand, // # 13965 - Blackhand's Breadth
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE }},
    {ITEM_MARK_OF_TYRANNY, ItemAcquireMethod::OnDemand, // # 13966 - Mark of Tyranny
    {}, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE }},
    {ITEM_EYE_OF_THE_BEAST, ItemAcquireMethod::OnDemand, // # 13968 - Eye of the Beast
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, { QUEST_GENERAL_DRAKKISATHS_DEMISE, QUEST_FOR_THE_HORDE }},

// Onyxia attunement
    {ITEM_DRAKEFIRE_AMULET, ItemAcquireMethod::OnDemand, // # 16309 - Drakefire Amulet
    {}, { QUEST_DRAKEFIRE_AMULET, QUEST_BLOOD_OF_THE_BLACK_DRAGON_CHAMPION }},

// Corruption of Earth and Seed (Maraudon)
    {ITEM_THRASH_BLADE, ItemAcquireMethod::OnDemand, // # 17705 - Thrash Blade
    { CLASS_WARRIOR, CLASS_ROGUE, CLASS_PALADIN, CLASS_HUNTER }, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A }},
    {ITEM_RESURGENCE_ROD, ItemAcquireMethod::OnDemand, // # 17743 - Resurgence Rod
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN, CLASS_MAGE, CLASS_WARLOCK }, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A }},
    {ITEM_VERDANT_KEEPERS_AIM, ItemAcquireMethod::OnDemand, // # 17753 - Verdant Keeper's Aim
    { CLASS_WARRIOR, CLASS_ROGUE, CLASS_HUNTER }, { QUEST_CORRUPTION_OF_EARTH_AND_SEED_H, QUEST_CORRUPTION_OF_EARTH_AND_SEED_A }},

// Onyxia's head
    {ITEM_DRAGONSLAYERS_SIGNET, ItemAcquireMethod::OnDemand, // # 18403 - Dragonslayer's Signet
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES }},
    {ITEM_ONYXIA_TOOTH_PENDANT, ItemAcquireMethod::OnDemand, // # 18404 - Onyxia Tooth Pendant
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES }},
    {ITEM_ONYXIA_BLOOD_TALISMAN, ItemAcquireMethod::OnDemand, // # 18406 - Onyxia Blood Talisman
    { CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID, CLASS_PALADIN }, { QUEST_FOR_ALL_TO_SEE, QUEST_CELEBRATING_GOOD_TIMES }},

// Nefarian's head (The Lord of Blackrock)
    {ITEM_MASTER_DRAGONSLAYERS_ORB, ItemAcquireMethod::OnDemand, // # 19366 - Master Dragonslayer's Orb
    { CLASS_MAGE, CLASS_WARLOCK, CLASS_PRIEST, CLASS_SHAMAN, CLASS_DRUID }, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A }},
    {ITEM_MASTER_DRAGONSLAYERS_MEDALLION, ItemAcquireMethod::OnDemand, // # 19383 - Master Dragonslayer's Medallion
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_DRUID }, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A }},
    {ITEM_MASTER_DRAGONSLAYERS_RING, ItemAcquireMethod::OnDemand, // # 19384 - Master Dragonslayer's Ring
    { CLASS_WARRIOR, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_DRUID, CLASS_SHAMAN }, { QUEST_LORD_OF_BLACKROCK_H, QUEST_LORD_OF_BLACKROCK_A }},

// Ossirian's head (The Fall of Ossirian)
    {ITEM_CHARM_OF_THE_SHIFTING_SANDS, ItemAcquireMethod::OnDemand, // # 21504 - Charm of the Shifting Sands
    {}, { QUEST_FALL_OF_OSSIRIAN }},
    {ITEM_CHOKER_OF_THE_SHIFTING_SANDS, ItemAcquireMethod::OnDemand, // # 21505 - Choker of the Shifting Sands
    {}, { QUEST_FALL_OF_OSSIRIAN }},
    {ITEM_PENDANT_OF_THE_SHIFTING_SANDS, ItemAcquireMethod::OnDemand, // # 21506 - Pendant of the Shifting Sands
    {}, { QUEST_FALL_OF_OSSIRIAN }},
    {ITEM_AMULET_OF_THE_SHIFTING_SANDS, ItemAcquireMethod::OnDemand, // # 21507 - Amulet of the Shifting Sands
    {}, { QUEST_FALL_OF_OSSIRIAN }},

// C'Thun's eye (The Savior of Kalimdor)
    {ITEM_RING_OF_THE_FALLEN_GOD, ItemAcquireMethod::OnDemand, // # 21709 - Ring of the Fallen God
    {}, { QUEST_SAVIOR_OF_KALIMDOR }},
    {ITEM_CLOAK_OF_THE_FALLEN_GOD, ItemAcquireMethod::OnDemand, // # 21710 - Cloak of the Fallen God
    {}, { QUEST_SAVIOR_OF_KALIMDOR }},
    {ITEM_AMULET_OF_THE_FALLEN_GOD, ItemAcquireMethod::OnDemand, // # 21712 - Amulet of the Fallen God
    {}, { QUEST_SAVIOR_OF_KALIMDOR }}
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
