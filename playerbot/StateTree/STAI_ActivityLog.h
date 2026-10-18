#pragma once

#include <deque>
#include <string>

namespace ai
{
    // Recent activity of one bot (tree nodes, executed actions) for the StateTree debug view.
    // Entries older than the retention are dropped. Written and read from the bot's own update.
    class STAI_ActivityLog
    {
    public:
        static constexpr uint32 retentionMs = 60000;

        struct Entry
        {
            uint64 id = 0;
            std::string kind;      // "node", "action"
            std::string name;      // node or action name
            std::string source;    // task class or strategy
            uint32 startMs = 0;    // WorldTimer::getMSTime()
            uint32 endMs = 0;
            bool open = true;      // still running, endMs not known yet
        };

        // starts an entry that is ended with End(id); returns its id
        uint64 Begin(const std::string& kind, const std::string& name, const std::string& source);
        void End(uint64 id);

        // entry with a known (expected) end; Shorten() can end it earlier
        uint64 Add(const std::string& kind, const std::string& name, const std::string& source, uint32 startMs, uint32 endMs);
        // ends the entry now if it was expected to end later
        void Shorten(uint64 id);

        // drops expired entries and returns the rest, oldest first
        const std::deque<Entry>& GetEntries();

    private:
        Entry* Find(uint64 id);
        void Prune(uint32 now);

        std::deque<Entry> entries;
        uint64 nextId = 1;
    };
}
