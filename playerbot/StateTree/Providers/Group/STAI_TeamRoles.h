#pragma once

#include "playerbot/PlayerbotAI.h"

#include "Entities/ObjectGuid.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <string>
#include <vector>

class Group;
class Player;

namespace ai
{
    // the spec and group roles of one member (PlayerSpec, SpecProvider_GlobalTask::GetSpecName)
    struct STAI_MemberRole
    {
        std::string specName;
        uint8 roles = BOT_ROLE_NONE;    // BotRoles
    };

    // Spec and roles of the bot and the members of its group on its map, for the global tasks that need them
    // (TeamProvider_GlobalTask, HealProvider_GlobalTask). Subscribe hooks the TeamSubsystem: group changes and talent
    // changes (OnTalentsChanged / OnMemberTalentsChanged) mark the roles stale, Update reads them again then, and
    // every 2s anyway (warriors and paladins tank by stance / Righteous Fury too, an aura). The callbacks only set a
    // flag: they may run on another thread (the world thread for group changes).
    class STAI_TeamRoles
    {
    public:
        ~STAI_TeamRoles() { Unsubscribe(); }

        void Subscribe(Player* bot);
        void Unsubscribe();

        // reads the roles again when they are stale or the refresh interval passed; true when they changed
        bool Update(Player* bot, uint32 elapsed);

        const std::map<ObjectGuid, STAI_MemberRole>& GetMembers() const { return members; }
        const STAI_MemberRole* Find(ObjectGuid guid) const;
        bool HasRole(ObjectGuid guid, BotRoles role) const;
        // the members with the role, in group order (the bot where it stands in the group)
        const std::vector<ObjectGuid>& WithRole(BotRoles role) const;

    private:
        void Read(Player* bot);

        void OnTeamChanged(Group* group, ObjectGuid self) { stale = true; }
        void OnMemberChanged(Group* group, ObjectGuid self, ObjectGuid member) { stale = true; }
        void OnTalentsChanged(ObjectGuid self) { stale = true; }
        void OnMemberTalentsChanged(ObjectGuid self, Player* member) { stale = true; }

        ObjectGuid subscribed;      // the bot whose TeamSubsystem events are subscribed, empty when none
        std::atomic<bool> stale{ true };
        uint32 sinceReadMs = 0;
        std::map<ObjectGuid, STAI_MemberRole> members;
        std::vector<ObjectGuid> tanks, healers, damage;
    };
}
