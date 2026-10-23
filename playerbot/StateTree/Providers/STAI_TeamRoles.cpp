#include "STAI_TeamRoles.h"

#include "playerbot/PlayerSpec.h"
#include "playerbot/StateTree/Providers/SpecProvider_GlobalTask.h"

#include "Groups/Group.h"
#include "Groups/TeamSubsystem.h"
#include "World/World.h"

using namespace ai;

namespace
{
    // stances and Righteous Fury change the roles without a talent change
    constexpr uint32 refreshIntervalMs = 2000;
}

void STAI_TeamRoles::Subscribe(Player* bot)
{
    if (!subscribed.IsEmpty() || !bot)
        return;

    subscribed = bot->GetObjectGuid();
    stale = true;
    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
    {
        const ObjectGuid self = bot->GetObjectGuid();
        teams->AddOnJoinedTeam(self, this, &STAI_TeamRoles::OnTeamChanged);
        teams->AddOnLeftTeam(self, this, &STAI_TeamRoles::OnTeamChanged);
        teams->AddOnMemberJoinedTeam(self, this, &STAI_TeamRoles::OnMemberChanged);
        teams->AddOnMemberLeftTeam(self, this, &STAI_TeamRoles::OnMemberChanged);
        teams->AddOnTalentsChanged(self, this, &STAI_TeamRoles::OnTalentsChanged);
        teams->AddOnMemberTalentsChanged(self, this, &STAI_TeamRoles::OnMemberTalentsChanged);
    }
}

void STAI_TeamRoles::Unsubscribe()
{
    if (subscribed.IsEmpty())
        return;

    if (TeamSubsystem* teams = sWorld.GetSubsystem<TeamSubsystem>())
        teams->RemoveAll(subscribed, this);
    subscribed.Clear();
}

bool STAI_TeamRoles::Update(Player* bot, uint32 elapsed)
{
    sinceReadMs += elapsed;
    if (!stale && sinceReadMs < refreshIntervalMs)
        return false;

    sinceReadMs = 0;
    stale = false;
    const std::map<ObjectGuid, STAI_MemberRole> before = members;
    Read(bot);

    if (before.size() != members.size())
        return true;
    for (const auto& [guid, role] : members)
    {
        auto found = before.find(guid);
        if (found == before.end() || found->second.roles != role.roles || found->second.specName != role.specName)
            return true;
    }
    return false;
}

void STAI_TeamRoles::Read(Player* bot)
{
    members.clear();
    tanks.clear();
    healers.clear();
    damage.clear();
    if (!bot->IsInWorld())
        return;

    // only members on the bot's map: the others run on another map thread
    std::vector<Player*> players;
    if (Group* group = bot->GetGroup())
    {
        for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
        {
            Player* member = itr->getSource();
            if (member && member->IsInWorld() && member->GetMap() == bot->GetMap())
                players.push_back(member);
        }
    }
    if (std::find(players.begin(), players.end(), bot) == players.end())
        players.push_back(bot);

    for (Player* player : players)
    {
        STAI_MemberRole& role = members[player->GetObjectGuid()];
        role.specName = SpecProvider_GlobalTask::GetSpecName(player);
        role.roles = uint8(PlayerSpec::GetPlayerRoles(player));
        if (role.roles & BOT_ROLE_TANK)
            tanks.push_back(player->GetObjectGuid());
        if (role.roles & BOT_ROLE_HEALER)
            healers.push_back(player->GetObjectGuid());
        if (role.roles & BOT_ROLE_DPS)
            damage.push_back(player->GetObjectGuid());
    }
}

const STAI_MemberRole* STAI_TeamRoles::Find(ObjectGuid guid) const
{
    auto found = members.find(guid);
    return found != members.end() ? &found->second : nullptr;
}

bool STAI_TeamRoles::HasRole(ObjectGuid guid, BotRoles role) const
{
    const STAI_MemberRole* member = Find(guid);
    return member && (member->roles & role) != 0;
}

const std::vector<ObjectGuid>& STAI_TeamRoles::WithRole(BotRoles role) const
{
    static const std::vector<ObjectGuid> none;
    switch (role)
    {
        case BOT_ROLE_TANK:   return tanks;
        case BOT_ROLE_HEALER: return healers;
        case BOT_ROLE_DPS:    return damage;
        default:              return none;
    }
}
