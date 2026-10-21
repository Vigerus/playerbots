#pragma once

#include "playerbot/PlayerbotMgr.h"

#include <string>
#include <vector>

/*
 * Bots of the automation tests: existing characters logged in as bots at a given position (their row in
 * `characters` is moved there first), updated by Update every world tick, logged out by LogoutAll which puts their
 * saved position back. Login is asynchronous: the bot is in the world a few ticks later (GetBot). The holder must
 * outlive the logins it started (the login callback is bound to it): keep it for the whole run.
 */
class FPlayerbotTestHolder : public PlayerbotHolder
{
public:
    FPlayerbotTestHolder() = default;

    // starts the login of the character as a bot on (mapId, x, y, z, orientation); false with the reason when the
    // character is unknown or already online
    bool Login(std::string const& name, uint32 mapId, float x, float y, float z, float orientation, std::string& error);

    // the bot of the character once it is in the world, nullptr before
    Player* GetBot(std::string const& name) const;

    // bot sessions (packets, logouts), call every world tick
    void Update(uint32 elapsed);

    // resurrects, logs out and puts back the saved position of every bot logged in by Login (a login still running
    // is logged out by Update when it ends)
    void LogoutAll();

protected:
    void OnBotLoginInternal(Player* const /*bot*/) override {}

private:
    struct FSavedPosition
    {
        std::string Name;
        uint32 Guid = 0;
        uint32 MapId = 0;
        float X = 0.0f, Y = 0.0f, Z = 0.0f, Orientation = 0.0f;
        bool LoggedOut = false;
    };

    void Logout(FSavedPosition const& login);

    std::vector<FSavedPosition> m_logins;
};
