#pragma once

class Player;
class ConfigAccess;

enum ItemAcquireMethod
{
    Constant,
    Mail
};

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
    

private:
    void Init();

    void Save(uint32 guildId, uint32 itemType, uint32 itemId);

    void OnItemCreated(Player* player, uint32 itemId);
    void OnAuraApplied(Unit* unit, uint32 itemId);

    std::map<uint32, std::vector<uint32>> guildItemsMap;
    std::map<uint32, std::vector<uint32>> guildBuffsMap;

    std::map<uint32, ItemAcquireMethod> progressItemsMap;
};