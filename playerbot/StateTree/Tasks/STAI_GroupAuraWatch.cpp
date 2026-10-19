#include "STAI_GroupAuraWatch.h"

#include "Groups/Group.h"
#include "Groups/TeamSubsystem.h"
#include "Spells/SpellAuraWorldSubsystem.h"
#include "World/World.h"

#include <algorithm>

using namespace ai;

void STAI_GroupAuraWatchContext::Subscribe()
{
    if (subscribed || !bot)
        return;

    subscribed = true;
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
    {
        const ObjectGuid self = bot->GetObjectGuid();
        teams->AddOnJoinedTeam(self, this, &STAI_GroupAuraWatchContext::OnJoinedTeam);
        teams->AddOnLeftTeam(self, this, &STAI_GroupAuraWatchContext::OnLeftTeam);
        teams->AddOnMemberJoinedTeam(self, this, &STAI_GroupAuraWatchContext::OnMemberJoinedTeam);
        teams->AddOnMemberLeftTeam(self, this, &STAI_GroupAuraWatchContext::OnMemberLeftTeam);
    }
    RefreshAuraTargets();
}

void STAI_GroupAuraWatchContext::Unsubscribe()
{
    if (!subscribed)
        return;

    subscribed = false;
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->RemoveAll(bot->GetObjectGuid(), this);
    if (SpellAuraWorldSubsystem* auras = sWorld.GetSubsystem<SpellAuraWorldSubsystem>())
        for (ObjectGuid guid : auraTargets)
            auras->RemoveAll(guid, this);
    auraTargets.clear();
}

void STAI_GroupAuraWatchContext::RefreshAuraTargets(Group* leftGroup)
{
    SpellAuraWorldSubsystem* auras = sWorld.GetSubsystem<SpellAuraWorldSubsystem>();
    if (!auras)
        return;

    std::vector<ObjectGuid> targets = { bot->GetObjectGuid() };
    Group* group = bot->GetGroup();
    if (group && group != leftGroup)
        for (const Group::MemberSlot& slot : group->GetMemberSlots())
            if (slot.guid != bot->GetObjectGuid())
                targets.push_back(slot.guid);

    for (ObjectGuid guid : auraTargets)
        if (std::find(targets.begin(), targets.end(), guid) == targets.end())
            auras->RemoveAll(guid, this);

    for (ObjectGuid guid : targets)
    {
        if (std::find(auraTargets.begin(), auraTargets.end(), guid) != auraTargets.end())
            continue;
        auras->AddOnAuraApplied(guid, this, &STAI_GroupAuraWatchContext::OnAuraApplied);
        auras->AddOnAuraRemoved(guid, this, &STAI_GroupAuraWatchContext::OnAuraRemoved);
    }

    auraTargets = std::move(targets);
}

std::vector<Player*> STAI_GroupAuraWatchContext::GetReachableMembers(Group* leftGroup) const
{
    std::vector<Player*> members;
    if (!bot->IsInWorld() || !bot->IsAlive())
        return members;

    Group* group = bot->GetGroup();
    if (group && group != leftGroup)
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (member && member->IsInWorld() && member->IsAlive() && member->GetMap() == bot->GetMap())
                members.push_back(member);
        }
    }
    if (std::find(members.begin(), members.end(), bot) == members.end())
        members.push_back(bot);
    return members;
}

void STAI_GroupAuraWatchContext::OnAuraApplied(Unit* target, SpellAuraHolder* holder)
{
    // members on other maps change their auras on another thread, and are out of reach anyway
    if (target->GetMap() == bot->GetMap() && IsWatchedAura(holder))
        Evaluate();
}

void STAI_GroupAuraWatchContext::OnAuraRemoved(Unit* target, SpellAuraHolder* holder, AuraRemoveMode mode)
{
    if (target->GetMap() == bot->GetMap() && IsWatchedAura(holder))
        Evaluate();
}

void STAI_GroupAuraWatchContext::OnJoinedTeam(Group* group, ObjectGuid self)
{
    RefreshAuraTargets();
    Evaluate();
}

void STAI_GroupAuraWatchContext::OnLeftTeam(Group* group, ObjectGuid self)
{
    // a disbanding group is still the bot's group during the event
    RefreshAuraTargets(group);
    Evaluate(group);
}

void STAI_GroupAuraWatchContext::OnMemberJoinedTeam(Group* group, ObjectGuid self, ObjectGuid member)
{
    RefreshAuraTargets();
    Evaluate();
}

void STAI_GroupAuraWatchContext::OnMemberLeftTeam(Group* group, ObjectGuid self, ObjectGuid member)
{
    RefreshAuraTargets();
    Evaluate();
}
