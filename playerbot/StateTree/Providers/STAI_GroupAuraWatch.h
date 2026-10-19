#pragma once

#include "playerbot/StateTree/Tasks/STAI_Task.h"

#include "Entities/ObjectGuid.h"
#include "Entities/Unit.h"

#include <vector>

class Group;
class Player;
class SpellAuraHolder;

namespace ai
{
    // Base of the contexts of global tasks that watch auras on the bot and its group (BuffProvider_GlobalTask,
    // CurseProvider_GlobalTask): Subscribe hooks the TeamSubsystem (group changes) and the SpellAuraWorldSubsystem
    // (aura applied / removed on every member, self included); each event of a watched aura on the bot's map, and
    // each group change, calls Evaluate. Unsubscribed in OnExit (Unsubscribe) and when the context is destroyed.
    struct STAI_GroupAuraWatchContext : public STAI_TaskContext
    {
        ~STAI_GroupAuraWatchContext() override { Unsubscribe(); }

        void Subscribe();
        void Unsubscribe();

        // recomputes the result from the current auras (leftGroup: a group the bot is leaving, treated as no group)
        virtual void Evaluate(Group* leftGroup = nullptr) = 0;
        // the aura is one the task looks for
        virtual bool IsWatchedAura(const SpellAuraHolder* holder) const = 0;

        // the bot and the members on its map that are alive (leftGroup as in Evaluate)
        std::vector<Player*> GetReachableMembers(Group* leftGroup = nullptr) const;

        void OnAuraApplied(Unit* target, SpellAuraHolder* holder);
        void OnAuraRemoved(Unit* target, SpellAuraHolder* holder, AuraRemoveMode mode);
        void OnJoinedTeam(Group* group, ObjectGuid self);
        void OnLeftTeam(Group* group, ObjectGuid self);
        void OnMemberJoinedTeam(Group* group, ObjectGuid self, ObjectGuid member);
        void OnMemberLeftTeam(Group* group, ObjectGuid self, ObjectGuid member);

    private:
        // aura subscriptions of the current members, also on other maps (their events are ignored)
        void RefreshAuraTargets(Group* leftGroup = nullptr);

        bool subscribed = false;
        std::vector<ObjectGuid> auraTargets;
    };
}
