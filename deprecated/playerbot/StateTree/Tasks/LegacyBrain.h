#pragma once

#include "playerbot/BotState.h"

#include <filesystem>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;

namespace ai
{
    class Engine;

    // Condition of a brain entry ("if" in the file). A Test checks one key (class, spec, battleground, ...)
    // against its values; All/Any/Not combine children.
    struct LegacyCondition
    {
        enum class Type : uint8 { All, Any, Not, Test };

        Type type = Type::All;
        std::string key;
        std::vector<std::string> values;
        std::vector<LegacyCondition> children;
    };

    // One line of a StateNode_Legacy strategy list. Add/Remove/Change act on the engine; Group runs its
    // children (or elseChildren when the condition fails). Includes are spliced in as unconditional groups.
    struct LegacyEntry
    {
        enum class Kind : uint8 { Add, Remove, Change, Group };

        Kind kind = Kind::Group;
        std::vector<std::string> names;              // Add/Remove: strategies, Change: one strategy string
        std::shared_ptr<LegacyCondition> condition;   // empty = always
        std::vector<LegacyEntry> children;
        std::vector<LegacyEntry> elseChildren;
    };

    // Strategy list of a StateNode_Legacy task, loaded from a Legacy_*.json / Legacy_*.xml file
    // (<Config filename="..."/> of the task). Replaces AiFactory::AddDefault<State>Strategies for its engine.
    class LegacyBrain
    {
    public:
        std::string name;        // "brain" field, defaults to the file name
        std::string file;        // file name as configured
        std::string state;       // "NonCombat", "Combat", "Reaction", "Dead"
        std::set<std::string> passive;   // "passive" of the file and its includes, see Engine::SetPassiveStrategies
        std::vector<LegacyEntry> entries;

        // Throws std::runtime_error with a readable message (file, line) on parse/validation errors.
        static std::shared_ptr<const LegacyBrain> Load(const std::filesystem::path& path, const std::string& file);

        void Apply(Player* player, PlayerbotAI* ai, Engine* engine) const;
    };

    // Loaded brains by file, shared by every bot whose tree uses them.
    class LegacyBrainLibrary
    {
    public:
        static LegacyBrainLibrary& instance()
        {
            static LegacyBrainLibrary library;
            return library;
        }

        // brain of a config file name (relative to aiplayerbot.conf), loaded on first use; throws on errors
        std::shared_ptr<const LegacyBrain> Get(const std::string& file);
        // a loaded brain by its name (LegacyBrain::name, Engine::GetBrainName), nullptr when none is loaded
        std::shared_ptr<const LegacyBrain> FindByName(const std::string& name);

        // forget loaded files (config reload)
        void Clear();

    private:
        std::mutex mutex;
        std::map<std::string, std::shared_ptr<const LegacyBrain>> brains;
    };
}

#define sLegacyBrainLibrary ai::LegacyBrainLibrary::instance()
