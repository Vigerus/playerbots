#include "STAI_ActivityLog.h"

#include <algorithm>

using namespace ai;

uint64 STAI_ActivityLog::Begin(const std::string& kind, const std::string& name, const std::string& source)
{
    const uint32 now = WorldTimer::getMSTime();
    Prune(now);

    Entry entry;
    entry.id = nextId++;
    entry.kind = kind;
    entry.name = name;
    entry.source = source;
    entry.startMs = now;
    entries.push_back(std::move(entry));
    return entries.back().id;
}

void STAI_ActivityLog::End(uint64 id)
{
    if (Entry* entry = Find(id))
    {
        if (entry->open)
        {
            entry->endMs = WorldTimer::getMSTime();
            entry->open = false;
        }
    }
}

uint64 STAI_ActivityLog::Add(const std::string& kind, const std::string& name, const std::string& source, uint32 startMs, uint32 endMs)
{
    Prune(WorldTimer::getMSTime());

    Entry entry;
    entry.id = nextId++;
    entry.kind = kind;
    entry.name = name;
    entry.source = source;
    entry.startMs = startMs;
    entry.endMs = endMs;
    entry.open = false;
    entries.push_back(std::move(entry));
    return entries.back().id;
}

void STAI_ActivityLog::Shorten(uint64 id)
{
    Entry* entry = Find(id);
    if (!entry || entry->open)
        return;

    const uint32 now = WorldTimer::getMSTime();
    // endMs still in the future: the entry ends now
    if (WorldTimer::getMSTimeDiff(entry->startMs, now) < WorldTimer::getMSTimeDiff(entry->startMs, entry->endMs))
        entry->endMs = now;
}

const std::deque<STAI_ActivityLog::Entry>& STAI_ActivityLog::GetEntries()
{
    Prune(WorldTimer::getMSTime());
    return entries;
}

STAI_ActivityLog::Entry* STAI_ActivityLog::Find(uint64 id)
{
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        if (it->id == id)
            return &*it;
    return nullptr;
}

void STAI_ActivityLog::Prune(uint32 now)
{
    // open entries (a running node) are kept even when they started long ago
    entries.erase(std::remove_if(entries.begin(), entries.end(), [now](const Entry& entry)
    {
        return !entry.open && WorldTimer::getMSTimeDiff(entry.endMs, now) >= retentionMs;
    }), entries.end());
}
