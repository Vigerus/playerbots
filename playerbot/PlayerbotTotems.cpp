#include "PlayerbotTotems.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "Maps/InstanceSubsystem.h"
#include "Maps/Map.h"
#include "Server/DBCStores.h"
#include "Spells/SpellMgr.h"
#include "World/World.h"

#include <algorithm>
#include <sstream>

using namespace ai;

namespace
{
    // the table is made on first use, so a server without the sql update still keeps the selections
    void CreateTable()
    {
        static bool created = false;
        if (created)
            return;

        created = true;
        CharacterDatabase.DirectExecute("CREATE TABLE IF NOT EXISTS `ai_playerbot_totems` ("
            "`guid` int(10) unsigned NOT NULL, `map` int(10) unsigned NOT NULL, `stage` int(10) unsigned NOT NULL, "
            "`fire` int(10) unsigned NOT NULL DEFAULT '0', `earth` int(10) unsigned NOT NULL DEFAULT '0', "
            "`water` int(10) unsigned NOT NULL DEFAULT '0', `air` int(10) unsigned NOT NULL DEFAULT '0', "
            "PRIMARY KEY (`guid`, `map`, `stage`)) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='PlayerbotAI totem selections'");
    }
}

PlayerbotTotems::SetKey PlayerbotTotems::GetCurrentKey(Player* bot, Player* master)
{
    if (!bot->IsInWorld())
        return outdoorKey;

    Map* map = bot->GetMap();
    InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
    if (!map->IsDungeon() || !instances || !instances->HasStages(map->GetId()))
        return outdoorKey;

    // the master's stage like the stage points, the bot's own while the master is elsewhere
    const Player* where = master && master->IsInWorld() && master->GetMap() == map ? master : bot;
    const int32 stage = instances->FindStage(map->GetId(), where->GetPositionX(), where->GetPositionY(), where->GetPositionZ());
    if (stage < 0)
        return outdoorKey;

    return { map->GetId(), uint32(stage) };
}

const PlayerbotTotems::TotemSet& PlayerbotTotems::GetSet(Player* bot, const SetKey& key)
{
    Load(bot);
    auto itr = sets.find(key);
    return itr != sets.end() ? itr->second : sets[outdoorKey];
}

bool PlayerbotTotems::HasOwnSet(Player* bot, const SetKey& key)
{
    Load(bot);
    return sets.find(key) != sets.end();
}

bool PlayerbotTotems::Select(Player* bot, const SetKey& key, uint8 school, uint32 spellId)
{
    if (school >= MAX_TOTEM_SLOT)
        return false;

    if (spellId)
    {
        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        if (!spellInfo || GetTotemSchool(spellInfo) != school)
            return false;
        spellId = sSpellMgr.GetFirstSpellInChain(spellId);
    }

    Load(bot);
    // a stage without its own set starts from the outdoor set
    auto itr = sets.find(key);
    if (itr == sets.end())
        itr = sets.emplace(key, sets[outdoorKey]).first;

    if (itr->second[school] == spellId)
        return true;

    itr->second[school] = spellId;
    ++version;
    Save(bot, key);
    return true;
}

uint8 PlayerbotTotems::GetTotemSchool(const SpellEntry* spellInfo)
{
    for (uint32 effect : spellInfo->Effect)
    {
        switch (effect)
        {
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT1: return TOTEM_SLOT_FIRE;
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT2: return TOTEM_SLOT_EARTH;
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT3: return TOTEM_SLOT_WATER;
            case SPELL_EFFECT_SUMMON_TOTEM_SLOT4: return TOTEM_SLOT_AIR;
            default: break;
        }
    }
    return TOTEM_SLOT_NONE;
}

std::vector<uint32> PlayerbotTotems::GetKnownTotems(Player* bot, uint8 school)
{
    std::vector<uint32> totems;
    for (const auto& [spellId, spell] : bot->GetSpellMap())
    {
        if (spell.state == PLAYERSPELL_REMOVED || spell.disabled)
            continue;

        const SpellEntry* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
        if (!spellInfo || GetTotemSchool(spellInfo) != school)
            continue;

        const uint32 firstRank = sSpellMgr.GetFirstSpellInChain(spellId);
        if (std::find(totems.begin(), totems.end(), firstRank) == totems.end())
            totems.push_back(firstRank);
    }
    // the spell map is unordered: by id, which is the order the totems were added to the game in
    std::sort(totems.begin(), totems.end());
    return totems;
}

uint32 PlayerbotTotems::GetHighestKnownRank(Player* bot, uint32 spellId)
{
    if (!spellId)
        return 0;

    uint32 known = 0;
    uint32 ranks = 0;
    for (uint32 rank = sSpellMgr.GetFirstSpellInChain(spellId); rank && ranks < 32; rank = sSpellMgr.GetNextSpellInChain(rank), ++ranks)
        if (bot->HasSpell(rank))
            known = rank;
    return known;
}

const char* PlayerbotTotems::GetSchoolName(uint8 school)
{
    switch (school)
    {
        case TOTEM_SLOT_FIRE:  return "fire";
        case TOTEM_SLOT_EARTH: return "earth";
        case TOTEM_SLOT_WATER: return "water";
        case TOTEM_SLOT_AIR:   return "air";
        default:               return "";
    }
}

uint8 PlayerbotTotems::ParseSchool(const std::string& name)
{
    for (uint8 school = 0; school < MAX_TOTEM_SLOT; ++school)
        if (name == GetSchoolName(school))
            return school;
    return TOTEM_SLOT_NONE;
}

std::string PlayerbotTotems::DescribeKey(const SetKey& key)
{
    if (key == outdoorKey)
        return "outdoor";

    std::ostringstream out;
    const MapEntry* mapEntry = sMapStore.LookupEntry(key.first);
    out << (mapEntry ? mapEntry->name[0] : "map " + std::to_string(key.first)) << " stage " << key.second;
    if (InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>())
    {
        const std::vector<InstanceStageDef> stages = instances->GetStages(key.first);
        if (key.second < stages.size() && !stages[key.second].name.empty())
            out << " (" << stages[key.second].name << ")";
    }
    return out.str();
}

void PlayerbotTotems::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    sets[outdoorKey] = {};
    CreateTable();
    auto result = CharacterDatabase.PQuery("SELECT `map`, `stage`, `fire`, `earth`, `water`, `air` FROM `ai_playerbot_totems` WHERE `guid` = '%u'",
        bot->GetGUIDLow());
    if (!result)
        return;

    do
    {
        Field* fields = result->Fetch();
        TotemSet& set = sets[{ fields[0].GetUInt32(), fields[1].GetUInt32() }];
        for (uint8 school = 0; school < MAX_TOTEM_SLOT; ++school)
            set[school] = fields[2 + school].GetUInt32();
    }
    while (result->NextRow());
}

void PlayerbotTotems::Save(Player* bot, const SetKey& key)
{
    const TotemSet& set = sets[key];
    CharacterDatabase.PExecute("REPLACE INTO `ai_playerbot_totems` (`guid`, `map`, `stage`, `fire`, `earth`, `water`, `air`) VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u')",
        bot->GetGUIDLow(), key.first, key.second, set[TOTEM_SLOT_FIRE], set[TOTEM_SLOT_EARTH], set[TOTEM_SLOT_WATER], set[TOTEM_SLOT_AIR]);
}
