#pragma once
// host-side stand-ins for what the playerbots PCH gives the state tree sources

#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <chrono>
#include <string>
#include <cassert>

typedef std::int8_t int8;
typedef std::uint8_t uint8;
typedef std::int16_t int16;
typedef std::uint16_t uint16;
typedef std::int32_t int32;
typedef std::uint32_t uint32;
typedef std::int64_t int64;
typedef std::uint64_t uint64;

#define MANGOS_ASSERT(x) assert(x)

enum Classes : uint8
{
    CLASS_WARRIOR = 1, CLASS_PALADIN = 2, CLASS_HUNTER = 3, CLASS_ROGUE = 4, CLASS_PRIEST = 5,
    CLASS_DEATH_KNIGHT = 6, CLASS_SHAMAN = 7, CLASS_MAGE = 8, CLASS_WARLOCK = 9, CLASS_DRUID = 11
};

struct WorldTimer
{
    static inline uint32 fake = 1000;
    static uint32 getMSTime() { return fake; }
    static uint32 getMSTimeDiff(uint32 oldMs, uint32 newMs) { return newMs - oldMs; }
};

struct HostLog
{
    int errors = 0;
    bool quiet = false;
    void outError(const char* format, ...)
    {
        ++errors;
        va_list args;
        va_start(args, format);
        std::printf("ERROR: ");
        std::vprintf(format, args);
        std::printf("\n");
        va_end(args);
    }
    void outString(const char* format, ...)
    {
        if (quiet)
            return;
        va_list args;
        va_start(args, format);
        std::vprintf(format, args);
        std::printf("\n");
        va_end(args);
    }
    static void WaitBeforeContinueIfNeed() {}
};
inline HostLog hostLog;
#define sLog hostLog
using Log = HostLog;

class Player
{
public:
    const char* GetName() const { return "HostBot"; }
};
