#include "PlayerbotRtsc.h"

#include "playerbot/PlayerbotAI.h"

#include "Database/DatabaseEnv.h"
#include "Entities/Player.h"
#include "Maps/InstanceSubsystem.h"
#include "MotionGenerators/MotionMaster.h"
#include "Spells/Spell.h"
#include "World/World.h"

#include <sstream>

using namespace ai;

namespace
{
    // the legacy qualified value of a saved location, the start of its ai_playerbot_db_store rows
    const std::string savedPrefix = "RTSC saved location::";
    // spell visuals the master sees on the bot: selected / deselected
    constexpr uint32 selectVisual = 5036;
    constexpr uint32 deselectVisual = 6372;
    // a cast this close to the bot selects it
    constexpr float selectDistance = 10.0f;
    // the marker shown on saved locations
    constexpr uint32 markerEntry = 15631;
}

void PlayerbotRtsc::QueueCommand(const std::string& text, Player* requester)
{
    Request request;
    request.command = text;
    request.requester = requester->GetObjectGuid().GetRawValue();
    std::lock_guard<std::mutex> lock(queueLock);
    queue.push_back(request);
}

void PlayerbotRtsc::QueueSpell(Player* master, const WorldPacket& packet, bool anySelected)
{
    // CMSG_CAST_SPELL: <uint32 spell id><targets>
    WorldPacket p(packet);
    p.rpos(0);
    uint32 spellId = 0;
    p >> spellId;
    if (spellId != moveSpell)
        return;

    SpellCastTargets targets;
    p >> targets.ReadForCaster(master);
    if (!(targets.m_targetMask & TARGET_FLAG_DEST_LOCATION))
        return;

    Request request;
    request.spell = true;
    request.position = WorldPosition(master->GetMapId(), targets.getDestination());
    request.anySelected = anySelected;
    request.requester = master->GetObjectGuid().GetRawValue();
    std::lock_guard<std::mutex> lock(queueLock);
    queue.push_back(request);
}

void PlayerbotRtsc::Update(PlayerbotAI* ai)
{
    std::vector<Request> requests;
    {
        std::lock_guard<std::mutex> lock(queueLock);
        requests.swap(queue);
    }

    for (const Request& request : requests)
    {
        // the master controls its bots
        Player* requester = sObjectMgr.GetPlayer(ObjectGuid(request.requester));
        if (!requester || !requester->IsInWorld() || requester != ai->GetMaster())
            continue;

        if (request.spell)
            HandleSpell(ai, requester, request.position, request.anySelected);
        else
            HandleCommand(ai, requester, request.command);
    }
}

void PlayerbotRtsc::SetSelected(PlayerbotAI* ai, Player* requester, bool select)
{
    if (select != selected)
        requester->GetSession()->SendPlaySpellVisual(ai->GetBot()->GetObjectGuid(), select ? selectVisual : deselectVisual);
    selected = select;
}

void PlayerbotRtsc::ShowMarker(Player* bot, const WorldPosition& position)
{
    if (!bot->IsInWorld() || position.getMapId() != bot->GetMapId())
        return;
    if (Creature* marker = bot->SummonCreature(markerEntry, position.getX(), position.getY(), position.getZ(), position.getO(), TEMPSPAWN_TIMED_DESPAWN, 2000))
        marker->SetObjectScale(0.5f);
}

void PlayerbotRtsc::HandleCommand(PlayerbotAI* ai, Player* requester, const std::string& command)
{
    Player* bot = ai->GetBot();
    Load(bot);

    if (command == "reset")
    {
        if (requester->HasSpell(moveSpell))
        {
            requester->removeSpell(moveSpell);
            ai->TellPlayerNoFacing(requester, "RTS control spell removed.");
        }
        enabled = false;
        selected = false;
        nextAction.clear();
        saved.clear();
        Save(bot);
        return;
    }

    // every other command switches it on
    if (!requester->HasSpell(moveSpell))
    {
        requester->learnSpell(moveSpell, false);
        ai->TellPlayerNoFacing(requester, "Aedm (Awesome energetic do move) spell trained.");
    }
    if (!enabled)
    {
        enabled = true;
        ai->TellPlayerNoFacing(requester, "RTS control enabled.");
    }

    if (command.empty())
        return;

    if (command == "select")
        SetSelected(ai, requester, true);
    else if (command == "cancel")
    {
        SetSelected(ai, requester, false);
        nextAction.clear();
    }
    else if (command == "toggle")
        SetSelected(ai, requester, !selected);
    else if (command.rfind("save here ", 0) == 0)
    {
        const std::string name = command.substr(10);
        saved[name] = WorldPosition(bot);
        Save(bot);
        ShowMarker(bot, saved[name]);
    }
    else if (command.rfind("unsave ", 0) == 0)
    {
        saved.erase(command.substr(7));
        Save(bot);
    }
    // the next cast decides
    else if (command.rfind("save ", 0) == 0 || command == "move")
        nextAction = command;
    else if (command.rfind("show ", 0) == 0)
    {
        auto found = saved.find(command.substr(5));
        if (found != saved.end())
            ShowMarker(bot, found->second);
    }
    else if (command == "show")
    {
        std::ostringstream out;
        out << "saved: ";
        bool first = true;
        for (const auto& [name, position] : saved)
        {
            out << (first ? "" : ",") << name;
            first = false;
        }
        out << (first ? "none." : ".");
        ai->TellPlayerNoFacing(requester, out.str());
    }
    else if (command.rfind("go ", 0) == 0)
    {
        auto found = saved.find(command.substr(3));
        if (found != saved.end())
            MoveTo(ai, requester, found->second);
    }
    else if (command == "last")
    {
        if (lastSpell)
            MoveTo(ai, requester, lastSpell);
    }
    else if (command.rfind("jump", 0) == 0 || command.rfind("file ", 0) == 0)
        // TODO(strategy): "rtsc jump" (the legacy jump action) and "rtsc file save|load" (locations to log files)
        ai->TellError(requester, "rtsc " + command + ": not supported by the new AI yet");
    else
        ai->TellError(requester, "Usage: rtsc [reset|select|cancel|toggle|move|save <name>|save selected <name>|save here <name>|unsave <name>|show [<name>]|go <name>|last]");
}

void PlayerbotRtsc::HandleSpell(PlayerbotAI* ai, Player* requester, const WorldPosition& position, bool anySelected)
{
    // only rtsc teaches Aedm and rtsc reset removes it: a master casting it has RTS control on, also after a server
    // restart (enabled is not saved)
    enabled = true;

    Player* bot = ai->GetBot();
    lastSpell = position;
    if (nextAction.empty())
    {
        // while some bot of the master is selected a cast moves the selected bots there (and deselects them), the
        // bots near the spot are not selected; with none selected a cast next to the bot selects it
        if (anySelected)
        {
            if (selected)
            {
                SetSelected(ai, requester, false);
                SaveDungeonPosition(ai, requester, position);
                MoveTo(ai, requester, position);
            }
        }
        else if (position.getMapId() == bot->GetMapId() &&
            bot->GetDistance(position.getX(), position.getY(), position.getZ()) <= selectDistance)
            SetSelected(ai, requester, true);
        return;
    }

    if (nextAction == "move")
    {
        SaveDungeonPosition(ai, requester, position);
        MoveTo(ai, requester, position);
        return;
    }

    // "save <name>" / "save selected <name>"
    const bool onlySelected = nextAction.rfind("save selected ", 0) == 0;
    const std::string name = nextAction.substr(onlySelected ? 14 : 5);
    nextAction.clear();
    if (onlySelected && !selected)
        return;

    Load(bot);
    saved[name] = position;
    Save(bot);
    ShowMarker(bot, position);
}

void PlayerbotRtsc::MoveTo(PlayerbotAI* ai, Player* requester, const WorldPosition& position)
{
    Player* bot = ai->GetBot();
    if (!bot->IsInWorld() || position.getMapId() != bot->GetMapId() || !bot->IsAlive())
        return;

    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    // the path comes from the PathfindingWorker; the trees' movement (Follow player) takes over when it runs again
    bot->GetMotionMaster()->MovePointAsync(0, position.getX(), position.getY(), position.getZ());
}

void PlayerbotRtsc::SaveDungeonPosition(PlayerbotAI* ai, Player* requester, const WorldPosition& position)
{
    Player* bot = ai->GetBot();
    InstanceSubsystem* instances = sWorld.GetSubsystem<InstanceSubsystem>();
    if (!instances || !bot->IsInWorld() || position.getMapId() != bot->GetMapId() || !instances->HasStages(bot->GetMapId()))
        return;

    // the point of the stage the master stands in, the cleared or the uncleared one by the stage's state now; nothing
    // while the master is outside every stage
    if (requester->GetMapId() != bot->GetMapId())
        return;
    const int32 stage = instances->FindStage(bot->GetMapId(), requester->GetPositionX(), requester->GetPositionY(), requester->GetPositionZ());
    if (stage < 0)
        return;

    const bool cleared = instances->IsStageCleared(bot->GetMap(), uint32(stage));
    ai->GetDungeonPositions().Set(bot, bot->GetMapId(), uint32(stage), cleared, { position.getX(), position.getY(), position.getZ() });
    std::ostringstream out;
    out << "Dungeon " << (cleared ? "cleared" : "uncleared") << " position saved for stage " << stage;
    const std::vector<InstanceStageDef> stages = instances->GetStages(bot->GetMapId());
    if (uint32(stage) < stages.size())
        out << " (" << stages[stage].name << ")";
    ai->TellPlayer(requester, out.str());
}

void PlayerbotRtsc::Load(Player* bot)
{
    if (loaded)
        return;

    loaded = true;
    auto result = CharacterDatabase.PQuery("SELECT `value` FROM `ai_playerbot_db_store` WHERE `guid` = '" UI64FMTD "' AND `preset` = '' AND `key` = 'value'",
        bot->GetObjectGuid().GetRawValue());
    if (!result)
        return;

    do
    {
        // "RTSC saved location::<name>><map>|<x>|<y>|<z>|<o>"
        const std::string value = result->Fetch()[0].GetCppString();
        if (value.rfind(savedPrefix, 0) != 0)
            continue;
        const size_t separator = value.find('>', savedPrefix.size());
        if (separator == std::string::npos)
            continue;
        const WorldPosition position(value.substr(separator + 1));
        if (position)
            saved[value.substr(savedPrefix.size(), separator - savedPrefix.size())] = position;
    }
    while (result->NextRow());
}

void PlayerbotRtsc::Save(Player* bot)
{
    const uint64 guid = bot->GetObjectGuid().GetRawValue();
    CharacterDatabase.PExecute("DELETE FROM `ai_playerbot_db_store` WHERE `guid` = '" UI64FMTD "' AND `preset` = '' AND `key` = 'value' AND `value` LIKE 'RTSC saved location::%%'",
        guid);
    for (const auto& [name, position] : saved)
    {
        std::string value = savedPrefix + name + ">" + position.to_string();
        CharacterDatabase.escape_string(value);
        CharacterDatabase.PExecute("INSERT INTO `ai_playerbot_db_store` (`guid`, `preset`, `key`, `value`) VALUES ('" UI64FMTD "', '', 'value', '%s')",
            guid, value.c_str());
    }
}
