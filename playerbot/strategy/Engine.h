#pragma once

#include "Action.h"
#include "Queue.h"
#include "Trigger.h"
#include "Multiplier.h"
#include "AiObjectContext.h"
#include "Strategy.h"
#include "playerbot/BotState.h"

#include "playerbot/StateTree/Tasks/StateTreeTask.h"

#include <set>

namespace ai
{
    class ActionExecutionListener
    {
    public:
        virtual bool Before(Action* action, const Event& event) = 0;
        virtual bool AllowExecution(Action* action, const Event& event) = 0;
        virtual void After(Action* action, bool executed, const Event& event) = 0;
        virtual bool OverrideResult(Action* action, bool executed, const Event& event) = 0;
        virtual ~ActionExecutionListener() {};
    };

    // -----------------------------------------------------------------------------------------------------------------------

    class ActionExecutionListeners : public ActionExecutionListener
    {
    public:
        virtual ~ActionExecutionListeners();

    // ActionExecutionListener
    public:
        virtual bool Before(Action* action, const Event& event);
        virtual bool AllowExecution(Action* action, const Event& event);
        virtual void After(Action* action, bool executed, const Event& event);
        virtual bool OverrideResult(Action* action, bool executed, const Event& event);

    public:
        void Add(ActionExecutionListener* listener)
        {
            listeners.push_back(listener);
        }
        void Remove(ActionExecutionListener* listener)
        {
            listeners.remove(listener);
        }

    private:
        std::list<ActionExecutionListener*> listeners;
    };

    // -----------------------------------------------------------------------------------------------------------------------

    enum ActionResult
    {
        ACTION_RESULT_UNKNOWN,
        ACTION_RESULT_OK,
        ACTION_RESULT_IMPOSSIBLE,
        ACTION_RESULT_USELESS,
        ACTION_RESULT_FAILED
    };

    class Engine : public PlayerbotAIAware
    {
    public:
        Engine(PlayerbotAI* ai, AiObjectContext *factory, BotState state);

	    void Init();
        void addStrategy(const std::string& name);
		void addStrategies(std::string first, ...);
        bool removeStrategy(const std::string& name, bool init = true);
        bool HasStrategy(const std::string& name);
        Strategy* GetStrategy(const std::string& name) const;
        void removeAllStrategies();
        void toggleStrategy(const std::string& name);
        std::string ListStrategies();
        std::list<std::string_view> GetStrategies();
		bool ContainsStrategy(StrategyType type);
		void ChangeStrategy(const std::string& names);
		void PrintStrategies(Player* requester, const std::string& engineType);
        std::string GetLastAction()
        {
            if (const ActionRunContext* context = lastActionContext.get())
                return context->name;
            return "";
        };

        const Action* GetLastExecutedAction() const { return lastExecutedAction; }
        const ActionRunContext* GetLastActionContext() const { return lastActionContext.get(); }
        EActionRunStatus GetLastExecutedActionStatus() const { return lastExecutedActionStatus; }
        // increases with every executed action, lets callers notice that DoNextAction executed one
        uint32 GetExecutedActionCount() const { return executedActionCount; }

        // Passive strategies stay in the engine (HasStrategy, lists, state tree conditions) but add no triggers,
        // multipliers or actions; used for strategies whose behaviour moved to state tree tasks (follow, stay).
        void SetPassiveStrategies(const std::set<std::string>& names) { passiveStrategies = names; }
        bool IsPassiveStrategy(const std::string& name) const { return passiveStrategies.count(name) > 0; }

        // source of the default strategies: STAI brain name or "AiFactory" (shown by the StateTree debug view)
        const std::string& GetBrainName() const { return brainName; }
        void SetBrainName(const std::string& name) { brainName = name; }
        // map key (PlayerbotLegacyStrategies::GetMapKey) whose strategy overrides the brain was applied with
        uint32 GetBrainMap() const { return brainMap; }
        void SetBrainMap(uint32 map) { brainMap = map; }

    public:
	    virtual bool DoNextAction(Unit*, int depth, bool minimal, bool isStunned);
	    ActionResult ExecuteAction(const std::string& name, Event& event);
        bool CanExecuteAction(const std::string& name, bool isUseful = true, bool isPossible = true);

    public:
//         void AddActionExecutionListener(ActionExecutionListener* listener)
//         {
//             actionExecutionListeners.Add(listener);
//         }
//         void removeActionExecutionListener(ActionExecutionListener* listener)
//         {
//             actionExecutionListeners.Remove(listener);
//         }

    public:
	    virtual ~Engine(void);

    protected:
        bool MultiplyAndPush(NextAction** actions, float forceRelevance, bool skipPrerequisites, const Event& event, const char* pushType, const std::string& source = "");
        void Reset();
        void ProcessTriggers(bool minimal);
        void PushDefaultActions();
        void PushAgain(ActionNode* actionNode, float relevance, const Event& event, const std::string& source = "");
        ActionNode* CreateActionNode(const std::string& name);
        virtual Action* InitializeAction(ActionNode* actionNode);
        virtual bool ListenAndExecute(Action* action, Event& event);

    private:
        void LogAction(const char* format, ...);
        void LogValues();

    protected:
	    Queue queue;
	    std::list<TriggerNode*> triggers;
        std::list<Multiplier*> multipliers;
        AiObjectContext* aiObjectContext;
        std::map<std::string, Strategy*> strategies;
        //ActionExecutionListeners actionExecutionListeners;
        BotState state;
        std::unique_ptr<ActionRunContext> lastActionContext;
        Action* lastExecutedAction = nullptr;
        EActionRunStatus lastExecutedActionStatus = EActionRunStatus::Succeeded;
        std::string brainName;
        uint32 brainMap = 0xFFFFFFFF;       // none yet
        std::string executingSource;     // strategy of the action being executed
        std::set<std::string> passiveStrategies;
        uint32 executedActionCount = 0;

        std::map<Trigger*, Event> fires;

    public:
		bool testMode;
        bool initMode = true;
    };
}
