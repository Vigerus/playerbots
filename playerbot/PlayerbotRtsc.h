#pragma once

#include "playerbot/WorldPosition.h"

#include "Platform/Define.h"

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
class WorldPacket;

namespace ai
{
    // RTS control: the master moves its bots by casting Aedm (spell 30758, taught by the first "rtsc" command) on the
    // ground, like an RTS unit order. Commands (whispered, or in party chat to every bot), from the master only:
    //   rtsc                    switches it on (and teaches the master the spell); a cast of the spell switches it
    //                           on too, so it keeps working after a server restart
    //   rtsc reset              switches it off, forgets the selection and the saved locations, removes the spell
    //   rtsc select / cancel / toggle   selects / deselects the bot (spell visual on the bot)
    //   rtsc move               every cast moves the bot there (until rtsc cancel / reset)
    //   rtsc save <name>        the next cast is saved as location <name>; "save selected <name>" only when selected
    //   rtsc save here <name>   the bot's position is saved as <name>
    //   rtsc unsave <name>      forgets <name>
    //   rtsc show [<name>]      lists the saved locations / marks <name> for 2s
    //   rtsc go <name>          moves to the saved location
    //   rtsc last               moves to the last cast's spot
    // Without a pending command: while none of the master's bots is selected a cast within 10 yards of a bot selects
    // it; while some are, a cast moves the selected bots there and deselects them, and selects no other bot. In a
    // dungeon with stages a move also saves the spot as the bot's point for the stage the master stands in, the
    // cleared or the uncleared one by the stage's state (PlayerbotDungeonPositions, "Position Yourself" of the Dungeon
    // trees); nothing while the master is outside every stage.
    // Saved locations are kept in ai_playerbot_db_store as the legacy values ("RTSC saved location::<name>>map|x|y|z|o").
    // Commands and casts are queued from the chat / the master's session and run in the bot's update (Update).
    class PlayerbotRtsc
    {
    public:
        static constexpr uint32 moveSpell = 30758;  // Aedm (Awesome energetic do move)

        // "rtsc ..." (text after "rtsc"), from HandleCommand
        void QueueCommand(const std::string& text, Player* requester);
        // the master's CMSG_CAST_SPELL, from HandleMasterIncomingPacket (the master's session). anySelected: one of
        // the master's bots was selected when the cast came (AnySelected, once for every bot of the cast)
        void QueueSpell(Player* master, const WorldPacket& packet, bool anySelected);
        // the bot is selected, read from the master's session
        bool IsSelected() const { return selected; }
        // runs what was queued (the bot's update, map thread)
        void Update(PlayerbotAI* ai);

    private:
        struct Request
        {
            bool spell = false;
            std::string command;    // !spell
            WorldPosition position; // spell: where it was cast
            bool anySelected = false;   // spell: some bot of the master was selected, the cast moves instead of selecting
            uint64 requester = 0;
        };

        void HandleCommand(PlayerbotAI* ai, Player* requester, const std::string& command);
        void HandleSpell(PlayerbotAI* ai, Player* requester, const WorldPosition& position, bool anySelected);
        void MoveTo(PlayerbotAI* ai, Player* requester, const WorldPosition& position);
        // in a dungeon with stages: the spot as the bot's cleared / uncleared point for the requester's stage (the
        // Position Yourself movement goal walks the bot there); false when nothing was saved (no stages, the requester
        // in no stage): the caller moves the bot itself
        bool SaveDungeonPosition(PlayerbotAI* ai, Player* requester, const WorldPosition& position);
        static void ShowMarker(Player* bot, const WorldPosition& position);
        void SetSelected(PlayerbotAI* ai, Player* requester, bool select);

        void Load(Player* bot);
        void Save(Player* bot);

        std::mutex queueLock;
        std::vector<Request> queue;

        bool enabled = false;
        std::atomic<bool> selected{false};     // also read from the master's session (IsSelected)
        std::string nextAction;     // "move", "save <name>", "save selected <name>"
        WorldPosition lastSpell;
        bool loaded = false;
        std::map<std::string, WorldPosition> saved;
    };
}
