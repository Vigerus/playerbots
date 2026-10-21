#pragma once
#include "Event.h"
#include "Value.h"
#include "AiObject.h"

class Unit;

namespace ai
{
    class NextAction;
    template<class T> class Value;
}

namespace ai
{
    class Trigger : public AiNamedObject
	{
	public:
        Trigger(PlayerbotAI* ai, std::string name = "trigger", int checkInterval = 1) : AiNamedObject(ai, name)
            ,checkInterval(checkInterval)
            ,lastCheckTime(time(0) - rand() % checkInterval)
        {
        }
        virtual ~Trigger() {}

	public:
        virtual Event Check();
        virtual void ExternalEvent(std::string param, Player* owner = NULL) {}
        virtual void ExternalEvent(WorldPacket &packet, Player* owner = NULL) {}
        virtual void ExternalEventForce(std::string param, Player* owner = NULL)
        {
            this->param = param;
            this->owner = owner;
            this->triggered = true;
        }
        virtual bool IsActive() { return false; }
        virtual bool IsAlreadyTriggered() { return triggered; }
        // fires only through ExternalEvent (chat commands, packets): the engine checks it only once triggered
        virtual bool IsEventDriven() const { return false; }
        // reads only the bot's own health / power conditions (PlayerbotAI::GetSelfThresholds): the reaction engine
        // wakes on their switches instead of checking it
        virtual bool IsResourceDriven() { return false; }
        // any other trigger: the reaction engine checks it at least this often, 250ms; with a check interval (whole
        // seconds, needCheck) every second, needCheck still lets it through once per interval
        virtual uint32 GetPollIntervalMs() const { return checkInterval >= 2 ? 1000 : 250; }
        virtual NextAction** getHandlers() { return NULL; }
        void Update() {}  //Nonfunctional see AiObjectContext::Update() to enable.
        virtual void Reset() { triggered = false; }
        virtual Unit* GetTarget();
        virtual Value<Unit*>* GetTargetValue();
        virtual std::string GetTargetName() { return "self target"; }

		bool needCheck() {
		    if (checkInterval < 2) return true;

		    time_t now = time(0);
			if (!lastCheckTime || now - lastCheckTime >= checkInterval) {
			    lastCheckTime = now;
				return true;
			}
			return false;
		}

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "dummy"; } //Must equal iternal name
        virtual std::string GetHelpDescription() { return "This is a trigger."; }
        virtual std::vector<std::string> GetUsedTriggers() { return {}; }
        virtual std::vector<std::string> GetUsedValues() { return {}; }
#endif

    protected:
		int checkInterval;
		time_t lastCheckTime;
        std::string param;
        bool triggered = false;
        Player* owner = nullptr;
	};


    class TriggerNode
    {
    public:
        TriggerNode(std::string name, NextAction** handlers = NULL)
        {
            this->name = name;
            this->handlers = handlers;
            this->trigger = NULL;
        }
        virtual ~TriggerNode();

    public:
        Trigger* getTrigger() { return trigger; }
        void setTrigger(Trigger* trigger) { this->trigger = trigger; }
        std::string getName() { return name; }
        // strategy that added this trigger (set by Engine::Init)
        const std::string& getStrategy() const { return strategy; }
        void setStrategy(const std::string& strategy) { this->strategy = strategy; }

    public:
        NextAction** getHandlers();
        float getFirstRelevance();
    private:
        std::string name;
        Trigger* trigger;
        NextAction** handlers;
        std::string strategy;
    };
}
