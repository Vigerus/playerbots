#include "harness.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/StateTree/Conditions/Condition_Compare.h"
#include "playerbot/StateTree/Conditions/STAI_BuiltinConditions.h"
#include "playerbot/StateTree/Considerations/STAI_BuiltinConsiderations.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"
#include "playerbot/StateTree/Tasks/STAI_FinishTreeTask.h"
#include "playerbot/StateTree/Tasks/STAI_SendEventTask.h"
#include "playerbot/StateTree/Tasks/Task_RunSubtree.h"
#include "playerbot/StateTree/Tasks/Task_Wait.h"

#include <filesystem>
#include <iostream>
#include <set>

using namespace ai;

namespace
{
    // deterministic test task: result="Succeeded|Failed|Running" in its enter, ticks="n": finishes after n ticks instead
    class Host_Result : public STAI_TaskBase
    {
    public:
        struct Context : public STAI_TaskContext { int ticks = 0; };

        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config)
        {
            auto task = std::make_unique<Host_Result>();
            const std::string result = config.get<std::string>("<xmlattr>.result", "Succeeded");
            task->result = result == "Failed" ? EActionRunStatus::Failed : result == "Running" ? EActionRunStatus::Running : EActionRunStatus::Succeeded;
            task->ticks = std::stoi(config.get<std::string>("<xmlattr>.ticks", "0"));
            return task;
        }
        virtual const char* GetNodeClass() const override { return "Host_Result"; }
        virtual std::string GetName() const override { return "Result"; }
        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Context>(); }
        virtual bool ShouldCallTick() const override { return true; }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& context) const override
        {
            ContextAs<Context>(context).ticks = 0;
            return ticks ? EActionRunStatus::Running : result;
        }
        virtual EActionRunStatus OnTick(STAI_TaskContext& context, uint32 elapsed, bool minimal) const override
        {
            return ticks && ++ContextAs<Context>(context).ticks >= ticks ? result : EActionRunStatus::Running;
        }

    private:
        EActionRunStatus result = EActionRunStatus::Succeeded;
        int ticks = 0;
    };

    // deterministic provider: count +1 every tick, onThird every third tick
    struct Host_CounterContext : public STAI_TaskContext
    {
        uint32 count = 0;
        bool flag = false;
        FSTAI_Delegate onThird;
    };

    class Host_Counter : public STAI_DataProviderBase
    {
    public:
        static void Validate(const STAI_TaskConfig& config) {}
        static std::unique_ptr<STAI_TaskBase> Create(const STAI_TaskConfig& config) { return std::make_unique<Host_Counter>(); }
        virtual const char* GetNodeClass() const override { return "Host_Counter"; }
        virtual std::string GetName() const override { return "Counter"; }
        virtual const STAI_TaskContextType& GetContextType() const override { return STAI_TaskContextType::Of<Host_CounterContext>(); }
        virtual bool ShouldCallTick() const override { return true; }
        virtual const std::vector<STAI_PropertyDef>& GetProperties() const override
        {
            static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("count", &Host_CounterContext::count), STAI_PropertyDef::Of("flag", &Host_CounterContext::flag) };
            return properties;
        }
        virtual const std::vector<STAI_DelegateDef>& GetDelegates() const override
        {
            static const std::vector<STAI_DelegateDef> delegates = { STAI_DelegateDef::Of("onThird", &Host_CounterContext::onThird) };
            return delegates;
        }

    protected:
        virtual EActionRunStatus OnEnterState(STAI_TaskContext& base) const override
        {
            ContextAs<Host_CounterContext>(base).count = 0;
            return EActionRunStatus::Running;
        }
        virtual EActionRunStatus OnTick(STAI_TaskContext& base, uint32 elapsed, bool minimal) const override
        {
            Host_CounterContext& context = ContextAs<Host_CounterContext>(base);
            ++context.count;
            context.flag = context.count >= 5;
            if (context.count % 3 == 0)
                context.onThird.Broadcast();
            return EActionRunStatus::Running;
        }
    };

    int failures = 0;

    void Check(bool ok, const std::string& what)
    {
        if (!ok)
        {
            ++failures;
            std::cout << "FAIL: " << what << "\n";
        }
    }

    std::string Leaf(const STAI_TreeInstance& tree)
    {
        const char* name = tree.GetActiveNodeName();
        return name ? name : "-";
    }

    // runs a test tree: the active leaf after every tick
    std::vector<std::string> Run(const std::string& file, int ticks, EActionRunStatus* finished = nullptr)
    {
        std::vector<std::string> leaves;
        std::shared_ptr<const STAI_TreeAsset> asset;
        try
        {
            asset = STAI_TreeAsset::Load(file);
        }
        catch (const std::exception& error)
        {
            Check(false, file + " does not load: " + error.what());
            return leaves;
        }

        Player player;
        STAI_ActivityLog log;
        STAI_TreeInstance tree(STAI_TaskContext(nullptr, &player, HLSStateType::Combat, &log), asset);
        tree.OnStart();
        for (int i = 0; i < ticks; ++i)
        {
            WorldTimer::fake += 100;
            const EActionRunStatus status = tree.OnTick(100, false);
            leaves.push_back(Leaf(tree));
            if (status != EActionRunStatus::Running)
            {
                leaves.back() = status == EActionRunStatus::Succeeded ? "<Succeeded>" : "<Failed>";
                if (finished)
                    *finished = status;
                break;
            }
        }
        tree.OnFinish();
        return leaves;
    }

    std::string Join(const std::vector<std::string>& leaves)
    {
        std::string text;
        for (const std::string& leaf : leaves)
            text += (text.empty() ? "" : " ") + leaf;
        return text;
    }

    void SyntheticTests(const std::string& directory)
    {
        // sequence through NextState, a wait, a completion transition, then the tree finishes
        {
            const std::string leaves = Join(Run(directory + "/test_sequence.xml", 20));
            std::cout << "sequence: " << leaves << "\n";
            Check(leaves == "B B B Done <Succeeded>" || leaves == "B B B <Succeeded>", "sequence order");
        }
        // utility picks Low first, High once the counter's consideration outweighs it, an event to the linked subtree,
        // its transition to Succeeded completes the Linked state, which goes to Done
        {
            const std::vector<std::string> leaves = Run(directory + "/test_utility.xml", 40);
            std::cout << "utility: " << Join(leaves) << "\n";
            Check(!leaves.empty() && leaves.front() == "Low", "utility starts low");
            Check(std::find(leaves.begin(), leaves.end(), "High") != leaves.end(), "utility reaches high");
            Check(std::find(leaves.begin(), leaves.end(), "SubWait") != leaves.end(), "event enters the linked subtree");
            Check(!leaves.empty() && leaves.back() == "<Succeeded>", "linked completion reaches Done");
        }
        // failure: the failed leaf waits its retry delay, the fallback runs meanwhile; a delayed, prioritized
        // transition and an OnDelegate transition
        {
            const std::vector<std::string> leaves = Run(directory + "/test_failure.xml", 40);
            std::cout << "failure: " << Join(leaves) << "\n";
            Check(!leaves.empty() && leaves.front() == "Fallback", "failed first state falls back");
            Check(std::find(leaves.begin(), leaves.end(), "Delayed") != leaves.end(), "delayed transition taken");
            Check(std::find(leaves.begin(), leaves.end(), "Third") != leaves.end(), "delegate transition taken");
        }
    }
}

namespace ai
{
    void RegisterBuiltinStateTreeTasks(STAI_NodeFactory& factory)
    {
        factory.RegisterCondition("Condition_Compare", { &Condition_Compare::Validate, &Condition_Compare::Create });
        factory.RegisterCondition("Condition_RandomChance", { &Condition_RandomChance::Validate, &Condition_RandomChance::Create });
        factory.RegisterCondition("Condition_ActiveStateLocation", { &Condition_ActiveStateLocation::Validate, &Condition_ActiveStateLocation::Create });
        factory.RegisterCondition("Condition_TimeInState", { &Condition_TimeInState::Validate, &Condition_TimeInState::Create });
        factory.RegisterConsideration("Consideration_Constant", { &Consideration_Constant::Validate, &Consideration_Constant::Create });
        factory.RegisterConsideration("Consideration_Number", { &Consideration_Number::Validate, &Consideration_Number::Create });

        factory.RegisterTask("STAI_SendEventTask", { &STAI_SendEventTask::Validate, &STAI_SendEventTask::Create });
        factory.RegisterTask("STAI_FinishTreeTask", { &STAI_FinishTreeTask::Validate, &STAI_FinishTreeTask::Create });
        factory.RegisterTask("STAI_GlobalTask", { &STAI_GlobalTask::Validate, &STAI_GlobalTask::Create });
        factory.RegisterTask("Task_RunSubtree", { &Task_RunSubtree::Validate, &Task_RunSubtree::Create });
        factory.RegisterTask("Task_Wait", { &Task_Wait::Validate, &Task_Wait::Create });
        factory.RegisterTask("Host_Result", { &Host_Result::Validate, &Host_Result::Create });
        factory.RegisterTask("Host_Counter", { &Host_Counter::Validate, &Host_Counter::Create });
        RegisterStubTasks(factory);
    }
}

// sthost <tests directory> <repository root>
int main(int argc, char** argv)
{
    const std::string tests = argc > 1 ? argv[1] : "tests";
    const std::string repository = argc > 2 ? argv[2] : "../..";
    _D_AIPLAYERBOT_CONFIG = repository + "/aiplayerbot.conf";

    SyntheticTests(tests);

    // every tree of the repository through the library, then each one run for a while with random stub tasks
    sSTAITreeLibrary.LoadFromConfig();
    Check(hostLog.errors == 0, "library load errors");

    const std::filesystem::path directory = repository + "/statetree";
    int files = 0;
    int statesSeen = 0;
    int statesTotal = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".xml" || entry.path().stem().string().rfind("STAI_", 0) != 0)
            continue;
        const std::string file = "statetree/" + entry.path().lexically_relative(directory).generic_string();
        std::shared_ptr<const STAI_TreeAsset> asset = sSTAITreeLibrary.Find(file);
        Check(asset != nullptr, file + " not in the library");
        if (!asset)
            continue;
        ++files;

        Player player;
        STAI_ActivityLog log;
        std::set<std::string> seen;
        int finishes = 0;
        {
            STAI_TreeInstance tree(STAI_TaskContext(nullptr, &player, HLSStateType::NonCombat, &log), asset);
            tree.OnStart();
            for (int i = 0; i < 400; ++i)
            {
                WorldTimer::fake += 100;
                if (tree.OnTick(100, false) != EActionRunStatus::Running)
                    ++finishes;
                for (const StateNodeDebugTree& stacked : [&]() { std::vector<StateNodeDebugTree> stack; tree.AppendDebugTreeStack(stack); return stack; }())
                    for (const StateNodeDebugChild& child : stacked.nodes)
                        if (child.active && stacked.name == asset->name)
                            seen.insert(child.name);
            }
            tree.OnFinish();
        }
        statesSeen += int(seen.size());
        statesTotal += int(asset->states.size());
        std::cout << file << ": " << seen.size() << "/" << asset->states.size() << " states active at some point, " << finishes << " finishes\n";
    }

    std::cout << files << " trees run, " << statesSeen << "/" << statesTotal << " states seen active, task enters " << hostStats.taskEnters
        << ", exits " << hostStats.taskExits << "\n";
    std::cout << (failures ? "FAILURES: " + std::to_string(failures) : std::string("ALL CHECKS PASSED")) << "\n";
    return failures ? 1 : 0;
}
