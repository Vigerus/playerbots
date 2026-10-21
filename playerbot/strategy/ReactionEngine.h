#pragma once

#include "Engine.h"

#include <atomic>
#include <memory>

namespace ai
{
    struct Reaction
    {
        bool IsValid() const { return action != nullptr; }
        bool IsActive() const { return IsValid() ? duration > 100U : false; }
        void Reset() { action = nullptr; duration = 0U; }

        bool Update(uint32 elapsed);
        
        void SetAction(Action* inAction);
        Action* GetAction() const { return action; }
        
        void SetEvent(const Event& inEvent) { event = Event(inEvent); }
        Event& GetEvent() { return event; }
        
        uint32 GetDuration() const { return duration; }
        void SetDuration(uint32 inDuration) { duration = inDuration; }
        bool ShouldInterruptCast() const { return action ? action->ShouldReactionInterruptCast() : false; }
        bool ShouldInterruptMovement() const { return action ? action->ShouldReactionInterruptMovement() : false; }

    private:
        Event event;
        Action* action = nullptr;
        uint32 duration = 0U;
    };

    class ReactionEngine : public Engine
    {
    public:
        ReactionEngine(PlayerbotAI* ai, AiObjectContext* factory, BotState engineState);

        void Reset();
        bool Update(uint32 elapsed, bool minimal, bool isStunned, bool& reactionFound);
        bool IsReacting() const { return ongoingReaction.IsValid(); }
        bool HasIncomingReaction() const { return incomingReaction.IsValid(); }
        void SetReactionDuration(const Action* action);
        const Reaction* GetReaction() const;

    
        // looks for a reaction only when one can have come up: a self health / power condition switched, the
        // strategies changed, a chat command or packet fired a trigger, a polled trigger is due (its
        // GetPollIntervalMs), or a fired trigger's reaction could not be done and is retried (cooldowns)
        bool FindReaction(bool isStunned);
    private:
        bool NeedsReactionCheck(uint32 now);
        // after a check: when the polled triggers are due, or the retry of a reaction that could not be done
        void ScheduleNextCheck(uint32 now, bool hadCandidates);

        bool StartReaction();
        void StopReaction();

        bool CanUpdateAIReaction() const;

        Action* InitializeAction(ActionNode* actionNode) override;
        bool ListenAndExecute(Action* action, Event& event) override;

    protected:
        Reaction incomingReaction;
        Reaction ongoingReaction;

    private:
        uint32 aiReactionUpdateDelay;

        // set by the bot's health / power condition switches (shared with the subscription, which outlives nothing)
        std::shared_ptr<std::atomic<bool>> wakeUp = std::make_shared<std::atomic<bool>>(true);
        uint32 checkedInitCount = 0;
        uint32 checkedExternalEvents = 0;
        bool hasNextCheck = false;
        uint32 nextCheckMs = 0;
    };
}
