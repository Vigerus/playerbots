#pragma once

#include "Entities/ObjectGuid.h"

#include <atomic>
#include <istream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class Player;
class Unit;
class Map;
class ConfigAccess;

enum ItemAcquireMethod
{
    Continuous,
    OnDemand
};

struct ItemProgressSet
{
    uint32 id;
    ItemAcquireMethod acquireMethod = ItemAcquireMethod::OnDemand;
    std::vector<uint8> eligible;
    std::vector<uint32> questIds;   // QuestItems: rewarding one of these quests (Horde / Alliance version) unlocks every item that has it
};

struct AuraProgressSet
{
    uint32 id;
    std::vector<uint8> eligible;
};

// What a guild has acquired: auras seen on its members, consumables / crafted items they made, quest items unlocked by
// rewarding a quest, enchants its enchanters made (with the reagents; then free for every enchanter of the guild who
// knows the recipe, Player::CanNoReagentCast). Guild members get the auras and the Continuous items (UpdateMap, on entering a map and every few
// seconds); OnDemand items are given by the GuildProgress addon ("BOT\tguildprogress list|give <item> <member>",
// answers "BOTGP").
class GuildProgressSubsystem
{
public:
    GuildProgressSubsystem();
    virtual ~GuildProgressSubsystem();
    static GuildProgressSubsystem& instance()
    {
        static GuildProgressSubsystem instance;
        return instance;
    }

    void Load(const ConfigAccess& config);

    std::vector<uint32> GetGuildBuffs(uint32 guildId) const;
    std::vector<uint32> GetGuildItems(uint32 guildId) const;

    std::vector<uint32> GetGuildBuffsForClass(uint32 guildId, uint8 classId) const;
    std::vector<uint32> GetGuildItemsForClass(uint32 guildId, uint8 classId) const;

    // any thread: a permanent enchant spell the guild has made once
    bool IsEnchantUnlocked(uint32 guildId, uint32 spellId) const;

    // map thread: the guild members on the map get the guild's auras / items, the addon requests of its players
    void UpdateMap(Map* map);
    void OnPlayerLeaveMap(Player* player);

    // any thread: queues a "BOT\tguildprogress ..." addon request, false when msg is not one
    bool HandleAddonRequest(Player* player, std::string const& msg);

private:
    struct Request
    {
        ObjectGuid player;
        std::string msg;
        bool internal;          // queued by the subsystem itself (spawn on the target, reply to the requester)
    };

    void Init();

    void Save(uint32 guildId, uint32 itemType, uint32 itemId);

    void OnItemCreated(Player* player, uint32 itemId);
    void OnAuraApplied(Unit* unit, uint32 itemId);
    void OnQuestRewarded(Player* player, uint32 questId);
    void OnItemEnchanted(Player* player, uint32 spellId);

    bool UnlockItem(uint32 guildId, uint32 itemId);
    void UnlockQuest(uint32 guildId, uint32 questId);
    bool IsItemUnlocked(uint32 guildId, uint32 itemId) const;

    void ApplyGuildProgress(Player* player);

    void QueueRequest(ObjectGuid guid, std::string const& msg, bool internal);
    void HandleRequest(Player* player, std::string const& msg, bool internal);
    void SendList(Player* player);
    void EnchantItem(Player* player, std::istream& in);
    void Reply(ObjectGuid requester, std::string const& text);

    void BuildReducedSet(const std::vector<uint32>& fullSet, std::vector<uint32>& reducedSet);

    mutable std::mutex m_lock;
    std::map<uint32, std::vector<uint32>> guildItemsMap;
    std::map<uint32, std::vector<uint32>> guildReducedItemsMap;
    std::map<uint32, std::vector<uint32>> guildBuffsMap;
    std::map<uint32, std::vector<uint32>> guildEnchantsMap;
    std::unordered_map<ObjectGuid, time_t> m_nextCheck;

    std::mutex m_requestLock;
    std::vector<Request> m_requests;
    std::atomic<bool> m_hasRequests = false;
    std::atomic<uint32> m_chunkSeq = 0;
};
