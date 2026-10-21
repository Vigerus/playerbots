#include "botpch.h"
#include "PlayerbotTestHelpers.h"

#include "playerbot/PlayerbotAI.h"
#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"

bool FPlayerbotTestHolder::Login(std::string const& name, uint32 mapId, float x, float y, float z, float orientation, std::string& error)
{
    ObjectGuid const guid = sObjectMgr.GetPlayerGuidByName(name);
    if (!guid)
    {
        error = "No character named " + name;
        return false;
    }

    if (sObjectMgr.GetPlayer(guid, false))
    {
        error = name + " is already online";
        return false;
    }

    auto result = CharacterDatabase.PQuery("SELECT map, position_x, position_y, position_z, orientation FROM characters WHERE guid = '%u'", guid.GetCounter());
    if (!result)
    {
        error = "No characters row for " + name;
        return false;
    }

    FSavedPosition saved;
    saved.Name = name;
    saved.Guid = guid.GetCounter();
    Field* fields = result->Fetch();
    saved.MapId = fields[0].GetUInt32();
    saved.X = fields[1].GetFloat();
    saved.Y = fields[2].GetFloat();
    saved.Z = fields[3].GetFloat();
    saved.Orientation = fields[4].GetFloat();
    m_logins.push_back(saved);

    // the login queries run after this update on the same asynchronous connection: the bot enters the world there
    CharacterDatabase.PExecute("UPDATE characters SET map = '%u', position_x = '%f', position_y = '%f', position_z = '%f', orientation = '%f', "
        "trans_x = 0, trans_y = 0, trans_z = 0, trans_o = 0, transguid = 0 WHERE guid = '%u'", mapId, x, y, z, orientation, saved.Guid);

    // its own account as master account: allowed whether or not it is a random bot account
    AddPlayerBot(saved.Guid, sObjectMgr.GetPlayerAccountIdByGUID(guid));
    return true;
}

Player* FPlayerbotTestHolder::GetBot(std::string const& name) const
{
    for (FSavedPosition const& login : m_logins)
    {
        if (login.Name != name || login.LoggedOut)
            continue;

        Player* bot = GetPlayerBot(login.Guid);
        return bot && bot->IsInWorld() ? bot : nullptr;
    }
    return nullptr;
}

void FPlayerbotTestHolder::Update(uint32 elapsed)
{
    UpdateSessions(elapsed);

    // logins that ended after LogoutAll
    for (auto itr = m_logins.begin(); itr != m_logins.end();)
    {
        if (itr->LoggedOut && GetPlayerBot(itr->Guid))
        {
            Logout(*itr);
            itr = m_logins.erase(itr);
        }
        else
            ++itr;
    }
}

void FPlayerbotTestHolder::LogoutAll()
{
    for (auto itr = m_logins.begin(); itr != m_logins.end();)
    {
        bool const online = GetPlayerBot(itr->Guid) != nullptr;
        Logout(*itr);

        // a login still running: logged out by Update when it ends
        if (online)
            itr = m_logins.erase(itr);
        else
        {
            itr->LoggedOut = true;
            ++itr;
        }
    }
}

void FPlayerbotTestHolder::Logout(FSavedPosition const& login)
{
    if (Player* bot = GetPlayerBot(login.Guid))
    {
        if (!bot->IsAlive())
        {
            bot->ResurrectPlayer(1.0f);
            bot->SpawnCorpseBones();
        }
        LogoutPlayerBot(login.Guid);
    }

    // queued after the logout save
    CharacterDatabase.PExecute("UPDATE characters SET map = '%u', position_x = '%f', position_y = '%f', position_z = '%f', orientation = '%f' WHERE guid = '%u'",
        login.MapId, login.X, login.Y, login.Z, login.Orientation, login.Guid);
}
