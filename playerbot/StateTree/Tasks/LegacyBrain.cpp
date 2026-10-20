#include "LegacyBrain.h"

#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/RandomPlayerbotMgr.h"
#include "playerbot/strategy/Engine.h"

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>

using namespace ai;
using boost::property_tree::ptree;

namespace
{
    constexpr int maxIncludeDepth = 8;

    const std::pair<uint8, const char*> classNames[] =
    {
        { CLASS_WARRIOR, "Warrior" },
        { CLASS_PALADIN, "Paladin" },
        { CLASS_HUNTER, "Hunter" },
        { CLASS_ROGUE, "Rogue" },
        { CLASS_PRIEST, "Priest" },
        { CLASS_SHAMAN, "Shaman" },
        { CLASS_MAGE, "Mage" },
        { CLASS_WARLOCK, "Warlock" },
        { CLASS_DRUID, "Druid" },
#ifdef MANGOSBOT_TWO
        { CLASS_DEATH_KNIGHT, "DeathKnight" },
#endif
    };

    const std::pair<BattleGroundTypeId, const char*> battlegroundNames[] =
    {
        { BATTLEGROUND_AV, "AV" },
        { BATTLEGROUND_WS, "WS" },
        { BATTLEGROUND_AB, "AB" },
#ifndef MANGOSBOT_ZERO
        { BATTLEGROUND_EY, "EY" },
#endif
#ifdef MANGOSBOT_TWO
        { BATTLEGROUND_IC, "IC" },
#endif
    };

    // condition keys, see statetree/README.md
    const std::set<std::string> boolTests = { "battleground", "arena", "autonomous", "freeBot", "inGroup", "groupLeader", "realPlayerMaster", "hasMaster", "masterIsBot" };
    const std::set<std::string> valueTests = { "class", "spec", "minLevel", "maxLevel", "bgType", "hasStrategy", "chance", "config" };
    const std::set<std::string> knownBattlegrounds = { "AV", "WS", "AB", "EY", "IC" }; // EY/IC never match on expansions without them

    std::optional<bool> GetConfigFlag(const std::string& name)
    {
        if (name == "jumpWithPlayer") return sPlayerbotAIConfig.jumpWithPlayer;
        if (name == "jumpInBg") return sPlayerbotAIConfig.jumpInBg;
        if (name == "jumpFollow") return sPlayerbotAIConfig.jumpFollow;
        if (name == "jumpChase") return sPlayerbotAIConfig.jumpChase;
        if (name == "randomBotJoinLfg") return sPlayerbotAIConfig.randomBotJoinLfg;
        if (name == "randomBotJoinBG") return sPlayerbotAIConfig.randomBotJoinBG;
        if (name == "autoDoQuests") return sPlayerbotAIConfig.autoDoQuests;
        if (name == "llmChat") return sPlayerbotAIConfig.llmEnabled == 2;
        return std::nullopt;
    }

    std::optional<float> GetConfigChance(const std::string& name)
    {
        if (name == "jumpNoCombatChance") return sPlayerbotAIConfig.jumpNoCombatChance;
        if (name == "jumpMeleeInCombatChance") return sPlayerbotAIConfig.jumpMeleeInCombatChance;
        return std::nullopt;
    }

    const std::string* GetConfigStrategies(const std::string& name)
    {
        if (name == "combatStrategies") return &sPlayerbotAIConfig.combatStrategies;
        if (name == "nonCombatStrategies") return &sPlayerbotAIConfig.nonCombatStrategies;
        if (name == "reactStrategies") return &sPlayerbotAIConfig.reactStrategies;
        if (name == "deadStrategies") return &sPlayerbotAIConfig.deadStrategies;
        if (name == "randomBotCombatStrategies") return &sPlayerbotAIConfig.randomBotCombatStrategies;
        if (name == "randomBotNonCombatStrategies") return &sPlayerbotAIConfig.randomBotNonCombatStrategies;
        if (name == "randomBotReactStrategies") return &sPlayerbotAIConfig.randomBotReactStrategies;
        if (name == "randomBotDeadStrategies") return &sPlayerbotAIConfig.randomBotDeadStrategies;
        return nullptr;
    }

    bool IsMetaKey(const std::string& key)
    {
        return key == "<xmlattr>" || key == "<xmlcomment>" || key == "description" || key == "comment";
    }

    bool ParseBool(const std::string& value, const std::string& where)
    {
        const std::string lower = boost::algorithm::to_lower_copy(value);
        if (lower == "true" || lower == "1" || lower == "yes")
            return true;
        if (lower == "false" || lower == "0" || lower == "no")
            return false;
        throw std::runtime_error(where + ": expected true/false, got '" + value + "'");
    }

    void AppendList(std::vector<std::string>& result, const std::string& text)
    {
        std::vector<std::string> parts;
        boost::algorithm::split(parts, text, boost::algorithm::is_any_of(","));
        for (std::string& part : parts)
        {
            boost::algorithm::trim(part);
            if (!part.empty())
                result.push_back(part);
        }
    }

    // A value is either a scalar ("a, b"), a JSON array (["a", "b"]) or an XML attribute.
    std::vector<std::string> ValuesOf(const ptree& value)
    {
        std::vector<std::string> result;
        if (value.empty())
            AppendList(result, value.data());
        else
            for (const auto& [key, item] : value)
                if (!IsMetaKey(key))
                    AppendList(result, item.data());
        return result;
    }

    // JSON field or XML attribute
    const ptree* FindField(const ptree& node, const std::string& key)
    {
        if (auto child = node.get_child_optional(ptree::path_type(key, '\0')))
            return &*child;
        if (auto attributes = node.get_child_optional("<xmlattr>"))
            if (auto attribute = attributes->get_child_optional(ptree::path_type(key, '\0')))
                return &*attribute;
        return nullptr;
    }

    std::optional<std::string> GetString(const ptree& node, const std::string& key)
    {
        if (const ptree* field = FindField(node, key))
            return field->data();
        return std::nullopt;
    }

    LegacyCondition ParseCondition(const ptree& node, const std::string& where);

    void ParseConditionKey(LegacyCondition& all, const std::string& key, const ptree& value, const std::string& where)
    {
        const std::string context = where + " condition '" + key + "'";

        if (key == "any" || key == "all")
        {
            LegacyCondition group;
            group.type = key == "any" ? LegacyCondition::Type::Any : LegacyCondition::Type::All;
            for (const auto& [childKey, child] : value)
                if (!IsMetaKey(childKey))
                    group.children.push_back(ParseCondition(child, where));
            if (group.children.empty())
                throw std::runtime_error(context + ": needs at least one condition");
            all.children.push_back(std::move(group));
            return;
        }

        if (key == "not")
        {
            LegacyCondition negation;
            negation.type = LegacyCondition::Type::Not;
            negation.children.push_back(ParseCondition(value, where));
            all.children.push_back(std::move(negation));
            return;
        }

        LegacyCondition test;
        test.type = LegacyCondition::Type::Test;
        test.key = key;
        test.values = ValuesOf(value);

        if (test.values.empty())
            throw std::runtime_error(context + ": missing value");

        if (boolTests.count(key))
        {
            ParseBool(test.values.front(), context);
        }
        else if (!valueTests.count(key))
        {
            throw std::runtime_error(context + ": unknown condition");
        }
        else if (key == "class")
        {
            for (const std::string& name : test.values)
            {
                bool known = false;
                for (const auto& [id, className] : classNames)
                    known = known || boost::algorithm::iequals(name, className);
                if (!known && !boost::algorithm::iequals(name, "DeathKnight"))
                    throw std::runtime_error(context + ": unknown class '" + name + "'");
            }
        }
        else if (key == "spec" || key == "minLevel" || key == "maxLevel")
        {
            for (const std::string& number : test.values)
                if (number.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error(context + ": expected a number, got '" + number + "'");
        }
        else if (key == "bgType")
        {
            for (const std::string& name : test.values)
                if (!knownBattlegrounds.count(name))
                    throw std::runtime_error(context + ": unknown battleground '" + name + "' (use AV, WS, AB, EY, IC)");
        }
        else if (key == "chance")
        {
            const std::string& chance = test.values.front();
            if (!GetConfigChance(chance))
            {
                try { std::stof(chance); }
                catch (const std::exception&) { throw std::runtime_error(context + ": expected 0..1 or a config chance, got '" + chance + "'"); }
            }
        }
        else if (key == "config")
        {
            for (const std::string& name : test.values)
                if (!GetConfigFlag(name))
                    throw std::runtime_error(context + ": unknown config flag '" + name + "'");
        }

        all.children.push_back(std::move(test));
    }

    LegacyCondition ParseCondition(const ptree& node, const std::string& where)
    {
        LegacyCondition all;
        all.type = LegacyCondition::Type::All;

        for (const auto& [key, value] : node)
        {
            if (key == "<xmlattr>")
            {
                for (const auto& [attribute, attributeValue] : value)
                    ParseConditionKey(all, attribute, attributeValue, where);
            }
            else if (!IsMetaKey(key))
            {
                ParseConditionKey(all, key, value, where);
            }
        }

        if (all.children.empty())
            throw std::runtime_error(where + ": empty condition");

        return all;
    }

    struct ParsedFile
    {
        ptree tree;
        const ptree* brain = nullptr;
    };

    void ReadFile(const std::filesystem::path& path, ParsedFile& parsed)
    {
        const std::string extension = boost::algorithm::to_lower_copy(path.extension().string());
        try
        {
            if (extension == ".xml")
            {
                boost::property_tree::read_xml(path.string(), parsed.tree, boost::property_tree::xml_parser::trim_whitespace | boost::property_tree::xml_parser::no_comments);
                auto brain = parsed.tree.get_child_optional("brain");
                if (!brain)
                    throw std::runtime_error(path.filename().string() + ": missing <brain> root element");
                parsed.brain = &*brain;
            }
            else
            {
                boost::property_tree::read_json(path.string(), parsed.tree);
                parsed.brain = &parsed.tree;
            }
        }
        catch (const boost::property_tree::file_parser_error& error)
        {
            throw std::runtime_error(path.filename().string() + ":" + std::to_string(error.line()) + ": " + error.message());
        }
    }

    std::vector<LegacyEntry> ParseEntries(const ptree& list, const std::filesystem::path& directory, const std::string& where, int depth);

    // passive strategies of the brain being loaded, collected from the file and its includes
    thread_local std::set<std::string>* passiveSink = nullptr;

    void CollectPassive(const ptree& brainNode)
    {
        if (!passiveSink)
            return;
        if (const ptree* passive = FindField(brainNode, "passive"))
            for (const std::string& name : ValuesOf(*passive))
                passiveSink->insert(name);
    }

    std::vector<LegacyEntry> LoadIncludedEntries(const std::filesystem::path& directory, const std::string& file, int depth, const std::string& where)
    {
        if (depth > maxIncludeDepth)
            throw std::runtime_error(where + ": include depth exceeded (include loop?) at '" + file + "'");

        ParsedFile parsed;
        ReadFile(directory / file, parsed);
        CollectPassive(*parsed.brain);

        const ptree* strategies = FindField(*parsed.brain, "strategies");
        if (!strategies)
            return {};

        return ParseEntries(*strategies, directory, file, depth);
    }

    std::optional<LegacyEntry> ParseEntry(const ptree& node, const std::filesystem::path& directory, const std::string& where, int depth)
    {
        if (auto enabled = GetString(node, "enabled"))
            if (!ParseBool(*enabled, where + " 'enabled'"))
                return std::nullopt;

        LegacyEntry entry;
        int kinds = 0;

        if (const ptree* add = FindField(node, "add"))
        {
            entry.kind = LegacyEntry::Kind::Add;
            entry.names = ValuesOf(*add);
            ++kinds;
        }
        if (const ptree* remove = FindField(node, "remove"))
        {
            entry.kind = LegacyEntry::Kind::Remove;
            entry.names = ValuesOf(*remove);
            ++kinds;
        }
        if (auto change = GetString(node, "change"))
        {
            entry.kind = LegacyEntry::Kind::Change;
            if (change->rfind("config:", 0) == 0 && !GetConfigStrategies(change->substr(7)))
                throw std::runtime_error(where + ": unknown config strategy list '" + change->substr(7) + "'");
            entry.names = { *change };
            ++kinds;
        }
        if (const ptree* strategies = FindField(node, "strategies"))
        {
            entry.kind = LegacyEntry::Kind::Group;
            entry.children = ParseEntries(*strategies, directory, where, depth);
            ++kinds;
        }
        if (auto include = GetString(node, "include"))
        {
            entry.kind = LegacyEntry::Kind::Group;
            entry.children = LoadIncludedEntries(directory, *include, depth + 1, where);
            ++kinds;
        }

        if (kinds != 1)
            throw std::runtime_error(where + ": an entry needs exactly one of add / remove / change / strategies / include");

        if ((entry.kind == LegacyEntry::Kind::Add || entry.kind == LegacyEntry::Kind::Remove) && entry.names.empty())
            throw std::runtime_error(where + ": empty strategy list");

        if (const ptree* condition = FindField(node, "if"))
            entry.condition = std::make_shared<LegacyCondition>(ParseCondition(*condition, where));

        if (const ptree* elseList = FindField(node, "else"))
        {
            if (!entry.condition)
                throw std::runtime_error(where + ": 'else' without 'if'");
            entry.elseChildren = ParseEntries(*elseList, directory, where, depth);
        }

        return entry;
    }

    std::vector<LegacyEntry> ParseEntries(const ptree& list, const std::filesystem::path& directory, const std::string& where, int depth)
    {
        std::vector<LegacyEntry> entries;
        int index = 0;
        for (const auto& [key, child] : list)
        {
            if (IsMetaKey(key))
                continue;

            ++index;
            if (auto entry = ParseEntry(child, directory, where + " entry #" + std::to_string(index), depth))
                entries.push_back(std::move(*entry));
        }
        return entries;
    }

    struct ApplyContext
    {
        Player* player;
        PlayerbotAI* ai;
        Engine* engine;
        int specTab = -1;

        int GetSpecTab()
        {
            if (specTab < 0)
                specTab = AiFactory::GetPlayerSpecTab(player);
            return specTab;
        }
    };

    bool Evaluate(const LegacyCondition& condition, ApplyContext& context);

    bool EvaluateTest(const LegacyCondition& test, ApplyContext& context)
    {
        Player* player = context.player;
        PlayerbotAI* ai = context.ai;
        const std::string& key = test.key;
        const std::vector<std::string>& values = test.values;

        if (boolTests.count(key))
        {
            const bool expected = ParseBool(values.front(), key);
            bool actual = false;

            if (key == "battleground")
                actual = player->InBattleGround();
            else if (key == "arena")
            {
#ifndef MANGOSBOT_ZERO
                actual = !player->IsBeingTeleported() && player->InArena();
#endif
            }
            else if (key == "autonomous")
                actual = ai->IsRealPlayer() || sRandomPlayerbotMgr.IsFreeBot(player);
            else if (key == "freeBot")
                actual = sRandomPlayerbotMgr.IsFreeBot(player);
            else if (key == "inGroup")
                actual = player->GetGroup() != nullptr;
            else if (key == "groupLeader")
                actual = ai->IsGroupLeader();
            else if (key == "realPlayerMaster")
                actual = ai->HasRealPlayerMaster();
            else if (key == "hasMaster")
                actual = ai->GetMaster() != nullptr;
            else if (key == "masterIsBot")
                actual = ai->GetMaster() && ai->GetMaster()->GetPlayerbotAI();

            return actual == expected;
        }

        if (key == "class")
        {
            for (const auto& [id, name] : classNames)
                if (id == player->getClass())
                    for (const std::string& value : values)
                        if (boost::algorithm::iequals(value, name))
                            return true;
            return false;
        }

        if (key == "spec")
        {
            const std::string tab = std::to_string(context.GetSpecTab());
            return std::find(values.begin(), values.end(), tab) != values.end();
        }

        if (key == "minLevel")
            return player->GetLevel() >= std::stoul(values.front());

        if (key == "maxLevel")
            return player->GetLevel() <= std::stoul(values.front());

        if (key == "bgType")
        {
            if (!player->InBattleGround())
                return false;

            BattleGroundTypeId bgType = player->GetBattleGroundTypeId();
#ifdef MANGOSBOT_TWO
            if (bgType == BATTLEGROUND_RB)
                bgType = player->GetBattleGround()->GetTypeId(true);
#endif
            for (const auto& [id, name] : battlegroundNames)
                if (id == bgType)
                    return std::find(values.begin(), values.end(), name) != values.end();
            return false;
        }

        if (key == "hasStrategy")
        {
            for (const std::string& value : values)
                if (context.engine->HasStrategy(value))
                    return true;
            return false;
        }

        if (key == "chance")
        {
            const std::optional<float> configChance = GetConfigChance(values.front());
            const float chance = configChance ? *configChance : std::stof(values.front());
            return frand(0.0f, 1.0f) < chance;
        }

        if (key == "config")
        {
            for (const std::string& value : values)
                if (!GetConfigFlag(value).value_or(false))
                    return false;
            return true;
        }

        return false;
    }

    bool Evaluate(const LegacyCondition& condition, ApplyContext& context)
    {
        switch (condition.type)
        {
            case LegacyCondition::Type::All:
                for (const LegacyCondition& child : condition.children)
                    if (!Evaluate(child, context))
                        return false;
                return true;
            case LegacyCondition::Type::Any:
                for (const LegacyCondition& child : condition.children)
                    if (Evaluate(child, context))
                        return true;
                return false;
            case LegacyCondition::Type::Not:
                return !Evaluate(condition.children.front(), context);
            case LegacyCondition::Type::Test:
                return EvaluateTest(condition, context);
        }
        return false;
    }

    void ApplyEntries(const std::vector<LegacyEntry>& entries, ApplyContext& context)
    {
        for (const LegacyEntry& entry : entries)
        {
            if (entry.condition && !Evaluate(*entry.condition, context))
            {
                ApplyEntries(entry.elseChildren, context);
                continue;
            }

            switch (entry.kind)
            {
                case LegacyEntry::Kind::Add:
                    for (const std::string& name : entry.names)
                        context.engine->addStrategy(name);
                    break;
                case LegacyEntry::Kind::Remove:
                    for (const std::string& name : entry.names)
                        context.engine->removeStrategy(name);
                    break;
                case LegacyEntry::Kind::Change:
                {
                    const std::string& change = entry.names.front();
                    if (change.rfind("config:", 0) == 0)
                    {
                        if (const std::string* strategies = GetConfigStrategies(change.substr(7)))
                            context.engine->ChangeStrategy(*strategies);
                    }
                    else
                    {
                        context.engine->ChangeStrategy(change);
                    }
                    break;
                }
                case LegacyEntry::Kind::Group:
                    ApplyEntries(entry.children, context);
                    break;
            }
        }
    }
}

std::shared_ptr<const LegacyBrain> LegacyBrain::Load(const std::filesystem::path& path, const std::string& file)
{
    const std::filesystem::path directory = path.parent_path();

    ParsedFile parsed;
    ReadFile(path, parsed);

    auto brain = std::make_shared<LegacyBrain>();
    brain->file = file;
    brain->name = GetString(*parsed.brain, "brain").value_or(path.stem().string());
    brain->state = GetString(*parsed.brain, "state").value_or("");

    struct PassiveSinkScope
    {
        explicit PassiveSinkScope(std::set<std::string>* sink) { passiveSink = sink; }
        ~PassiveSinkScope() { passiveSink = nullptr; }
    } passiveScope(&brain->passive);

    CollectPassive(*parsed.brain);

    if (const ptree* strategies = FindField(*parsed.brain, "strategies"))
        brain->entries = ParseEntries(*strategies, directory, file, 0);

    return brain;
}

void LegacyBrain::Apply(Player* player, PlayerbotAI* ai, Engine* engine) const
{
    engine->SetPassiveStrategies(passive);

    ApplyContext context{ player, ai, engine };
    ApplyEntries(entries, context);
    engine->SetBrainName(name);
}

std::shared_ptr<const LegacyBrain> LegacyBrainLibrary::Get(const std::string& file)
{
    const std::filesystem::path path = STAI_TaskFactory::ResolvePath(file);
    const std::string key = path.string();

    std::lock_guard<std::mutex> lock(mutex);
    auto it = brains.find(key);
    if (it == brains.end())
        it = brains.emplace(key, LegacyBrain::Load(path, file)).first;
    return it->second;
}

std::shared_ptr<const LegacyBrain> LegacyBrainLibrary::FindByName(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [file, brain] : brains)
        if (brain && brain->name == name)
            return brain;
    return nullptr;
}

void LegacyBrainLibrary::Clear()
{
    std::lock_guard<std::mutex> lock(mutex);
    brains.clear();
}
