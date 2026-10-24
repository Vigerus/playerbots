#pragma once

#include "Platform/Define.h"

class PlayerbotAI;

namespace ai
{
    // Server-side version of the BotStateTree addon: a native window (Windows only) opened when the server starts,
    // with a drop-down of every active bot and, for the selected one, its States (the trees of the active state and
    // of Reaction, nodes, conditions, tasks, switches) and its History (the activity log, kept for
    // AiPlayerbot.DebugWindow.HistoryMinutes for every bot while the window runs). Spell icons are downloaded from
    // wowhead into spellicons/ next to the server and shown from there.
    //
    // The window runs on its own thread and never touches a bot: each bot lists itself and, while selected,
    // publishes a copy of its state from its own update (OnBotUpdate); the window's switches go the other way and
    // are applied there too.
    class PlayerbotDebugWindow
    {
    public:
        static PlayerbotDebugWindow& instance();

        // opens the window (no-op off Windows); historyMinutes: how long every bot keeps its activity log
        void Start(uint32 historyMinutes);
        // closes the window and waits for its threads (before the server's singletons go away)
        void Stop();
        bool IsRunning() const;

        // from PlayerbotAI::UpdateAI, on the bot's map thread
        void OnBotUpdate(PlayerbotAI* ai);
        // from ~PlayerbotAI: the bot leaves the drop-down
        void OnBotRemoved(uint64 guid);
    };
}

#define sPlayerbotDebugWindow ai::PlayerbotDebugWindow::instance()
