#pragma once

#include <deque>
#include <string>

namespace ai
{
    // Recent activity of one bot (tree nodes, executed actions) for the StateTree debug view.
    // Entries older than the retention (a minute, an hour with the server's debug window) are dropped. Written and
    // read from the bot's own update.
    // A run of the same kind, name and source as the newest entry (a node picked over and over, an action executed
    // again and again) is merged into it instead of flooding the log: the entry counts the runs and sums their time,
    // its start / end are the latest run's.
    class STAI_ActivityLog
    {
    public:
        // what the BotStateTree addon shows (its History tab: the last minute)
        static constexpr uint32 addonRetentionMs = 60000;
        // how long entries are kept, for every bot (PlayerbotDebugWindow: AiPlayerbot.DebugWindow.HistoryMinutes)
        static void SetRetention(uint32 ms) { retentionMs = ms < addonRetentionMs ? addonRetentionMs : ms; }
        static uint32 GetRetention() { return retentionMs; }

        struct Entry
        {
            uint64 id = 0;
            std::string kind;      // "node", "action"
            std::string name;      // node or action name
            std::string source;    // task class or strategy
            uint32 startMs = 0;    // WorldTimer::getMSTime()
            uint32 endMs = 0;
            bool open = true;      // still running, endMs not known yet
            int8 result = -1;      // -1 none, 0 failed, 1 successful (the latest run)
            uint32 count = 1;      // runs merged into the entry
            uint32 doneMs = 0;     // summed duration of the runs before the latest one
            uint32 failures = 0;   // runs that failed (result 0), the latest one included
        };

        // starts an entry that is ended with End(id); returns its id
        uint64 Begin(const std::string& kind, const std::string& name, const std::string& source);
        // the same, started at startMs (a task logged once it is still running after its first update); stacked
        // false: always a new entry, never merged into the newest one (STAI_Task::AreConsecutiveLogsStacked)
        uint64 BeginAt(const std::string& kind, const std::string& name, const std::string& source, uint32 startMs, bool stacked = true);
        // result: -1 none, 0 failed, 1 successful (a task's run)
        void End(uint64 id, int8 result = -1);

        // entry with a known (expected) end; Shorten() can end it earlier
        uint64 Add(const std::string& kind, const std::string& name, const std::string& source, uint32 startMs, uint32 endMs);
        // ends the entry now if it was expected to end later
        void Shorten(uint64 id);

        // drops expired entries and returns the rest, oldest first
        const std::deque<Entry>& GetEntries();

    private:
        Entry* Find(uint64 id);
        void Prune(uint32 now);
        // the newest entry when it is of the same kind, name and source and finished: the run starting at startMs
        // is merged into it (the earlier run's time is summed); nullptr when a new entry is needed
        Entry* MergeRun(const std::string& kind, const std::string& name, const std::string& source, uint32 startMs);

        std::deque<Entry> entries;
        uint64 nextId = 1;
        // with an hour of entries a prune on every Begin would scan them all: at most once a second
        uint32 lastPruneMs = 0;

        static inline uint32 retentionMs = addonRetentionMs;
    };
}
