#pragma once
#include "PlayerbotMgr.h"
#include "PlayerbotAIBase.h"
#include "PlayerbotAIAware.h"
#include "ChatHelper.h"
#include "WorldPosition.h"
#include "ChatFilter.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "PlayerTalentSpec.h"
#include "PlayerbotDungeonPositions.h"
#include "PlayerbotTargetIcons.h"
#include "PlayerbotTotems.h"
#include "PlayerbotMovement.h"
#include "PlayerbotRtsc.h"
#include "PlayerbotPremadeSpecs.h"

#include <atomic>
#include <mutex>
#include <stack>
#include "IterateItemsMask.h"
#include "PerformanceMonitor.h"
#include "Qualified.h"
#include "RandomPlayerbotMgr.h"

#include "StateTree/StateMachine.h"
#include "delegates/Delegates.h"
#include "Entities/UnitResourceThresholds.h"

class Corpse;
class Player;
class PlayerbotMgr;
class ChatHandler;

using namespace ai;

bool IsAlliance(uint8 race);

class PlayerbotChatHandler: protected ChatHandler
{
public:
    explicit PlayerbotChatHandler(Player* pMasterPlayer) : ChatHandler(pMasterPlayer->GetSession()) {}
    void sysmessage(std::string str) { SendSysMessage(str.c_str()); }
    uint32 extractQuestId(std::string str);
    uint32 extractSpellId(std::string str)
    {
        char* source = (char*)str.c_str();
        return ExtractSpellIdFromLink(&source);
    }
};

class ChannelAcces
{
public:    
    struct PlayerInfo
    {
        ObjectGuid player;
        uint8 flags;       
    };

    typedef std::map<ObjectGuid, PlayerInfo> PlayerList;

    bool IsOn(ObjectGuid who) const { return m_players.find(who) != m_players.end(); }    
    std::string                 m_name;
    std::string                 m_password;
    ObjectGuid                  m_ownerGuid;
    PlayerList                  m_players;
    GuidSet                     m_banned;
    const ChatChannelsEntry* m_entry = nullptr;
    bool                        m_announcements = false;
    bool                        m_moderation = false;
    uint8                       m_flags = 0x00;
    // Custom features:
    bool                        m_static = false;
    bool                        m_realmzone = false;
};

namespace ai
{
    class WorldPosition;
    class GuidPosition;
    class PlayerbotDebugWindow;
    class IterateItemsVisitor;
    class FindItemVisitor;

	class MinValueCalculator {
	public:
		MinValueCalculator(float def = 0.0f) {
			param = NULL;
			minValue = def;
		}

	public:
		void probe(float value, void* p) {
			if (!param || minValue >= value) {
				minValue = value;
				param = p;
			}
		}

	public:
		void* param;
		float minValue;
	};
};

enum ImportantAreaId
{
    CITY = 3459
};

enum ChatChannelId
{
    GENERAL = 1,
    TRADE = 2,
    LOCAL_DEFENSE = 22,
    WORLD_DEFENSE = 23,
#ifdef MANGOSBOT_ZERO
    //Yes, for 1.12 it is 24
    LOOKING_FOR_GROUP = 24,
#else
    LOOKING_FOR_GROUP = 26,
#endif
    GUILD_RECRUITMENT = 25,
};

enum ChatChannelSource
{
    SRC_GUILD,
    SRC_WORLD,
    SRC_GENERAL,
    SRC_TRADE,
    SRC_LOOKING_FOR_GROUP,
    SRC_LOCAL_DEFENSE,
    SRC_WORLD_DEFENSE,
    SRC_GUILD_RECRUITMENT,

    SRC_SAY,
    SRC_WHISPER,
    SRC_EMOTE,
    SRC_TEXT_EMOTE,
    SRC_YELL,

    SRC_PARTY,
    SRC_RAID,

    SRC_UNDEFINED
};

enum HealingItemDisplayId
{
   HEALTHSTONE_DISPLAYID = 8026,
   MAJOR_HEALING_POTION = 24152,
   WHIPPER_ROOT_TUBER = 21974,
   NIGHT_DRAGON_BREATH = 21975,
   LIMITED_INVULNERABILITY_POTION = 24213,
   GREATER_DREAMLESS_SLEEP_POTION = 17403,
   SUPERIOR_HEALING_POTION = 15714,
   CRYSTAL_RESTORE = 2516,
   DREAMLESS_SLEEP_POTION = 17403,
   GREATER_HEALING_POTION = 15713,
   HEALING_POTION = 15712,
   LESSER_HEALING_POTION = 15711,
   DISCOLORED_HEALING_POTION = 15736,
   MINOR_HEALING_POTION = 15710,
   VOLATILE_HEALING_POTION = 24212,
   SUPER_HEALING_POTION = 37807,
   CRYSTAL_HEALING_POTION = 47132,
   FEL_REGENERATION_POTION = 37864,
   MAJOR_DREAMLESS_SLEEP_POTION = 37845,
};

enum RoguePoisonDisplayId
{
   DEADLY_POISON_DISPLAYID = 13707,
   CRIPPLING_POISON_DISPLAYID = 13708,
   CRIPPLING_POISON_DISPLAYID_II = 2947,
   MIND_POISON_DISPLAYID = 13709,
   INSTANT_POISON_DISPLAYID = 13710,
#ifdef MANGOSBOT_ZERO
   WOUND_POISON_DISPLAYID = 13708
#else
   WOUND_POISON_DISPLAYID = 37278
#endif
};

enum SharpeningStoneDisplayId
{
   ROUGH_SHARPENING_DISPLAYID = 24673,
   COARSE_SHARPENING_DISPLAYID = 24674,
   HEAVY_SHARPENING_DISPLAYID = 24675,
   SOLID_SHARPENING_DISPLAYID = 24676,
   DENSE_SHARPENING_DISPLAYID = 24677,
   CONSECRATED_SHARPENING_DISPLAYID = 24674,    // will not be used because bot can not know if it will face undead targets
   ELEMENTAL_SHARPENING_DISPLAYID = 21072,
   FEL_SHARPENING_DISPLAYID = 39192,
   ADAMANTITE_SHARPENING_DISPLAYID = 39193,
};

enum WeightStoneDisplayId
{
   ROUGH_WEIGHTSTONE_DISPLAYID = 24683,
   COARSE_WEIGHTSTONE_DISPLAYID = 24684,
   HEAVY_WEIGHTSTONE_DISPLAYID = 24685,
   SOLID_WEIGHTSTONE_DISPLAYID = 24686,
   DENSE_WEIGHTSTONE_DISPLAYID = 24687,
   FEL_WEIGHTSTONE_DISPLAYID = 39548,
   ADAMANTITE_WEIGHTSTONE_DISPLAYID = 39549,
};

#ifdef MANGOSBOT_ZERO
// m_zero
enum WizardOilDisplayId
{
    MINOR_WIZARD_OIL = 33194,
    LESSER_WIZARD_OIL = 33450,
    BRILLIANT_WIZARD_OIL = 33452,
    WIZARD_OIL = 33451,
    SUPERIOR_WIZARD_OIL = 47904,
    /// Blessed Wizard Oil = 26865,//scourge inv
};
// m_zero
enum ManaOilDisplayId
{
    MINOR_MANA_OIL = 33453,
    LESSER_MANA_OIL = 33454,
    BRILLIANT_MANA_OIL = 33455,
    SUPERIOR_MANA_OIL = 36862,
};
#else
enum WizardOilDisplayId
{
   MINOR_WIZARD_OIL     = 9731,
   LESSER_WIZARD_OIL    = 47903,
   BRILLIANT_WIZARD_OIL = 47901,
   WIZARD_OIL           = 47905,
   SUPERIOR_WIZARD_OIL  = 47904,
  /// Blessed Wizard Oil = 26865,//scourge inv
};

enum ManaOilDisplayId
{
   MINOR_MANA_OIL       = 34492,
   LESSER_MANA_OIL      = 47902,
   BRILLIANT_MANA_OIL   = 41488,
   SUPERIOR_MANA_OIL    = 36862,
};
#endif

enum ShieldWardDisplayId
{
   LESSER_WARD_OFSHIELDING = 38759,
   GREATER_WARD_OFSHIELDING = 38760,
};

enum class BotTypeNumber : uint8
{
    ACTIVITY_TYPE_NUMBER = 1,
    GROUPER_TYPE_NUMBER = 2,
    GUILDER_TYPE_NUMBER = 3,
    CHATFILTER_NUMBER = 4 ,
    DUMMY_ATTACK_NUMBER = 5, 
    RPG_PHASE_NUMBER = 6,
    RPG_STYLE_NUMBER = 7,
    WORLD_PVP_LOCATION = 8
};

enum class GrouperType : uint8
{
    SOLO = 0,
    MEMBER = 1,
    LEADER_2 = 2,
    LEADER_3 = 3,
    LEADER_4 = 4,
    LEADER_5 = 5,
#ifndef MANGOSBOT_ZERO
    RAIDER_10 = 10,
    RAIDER_MAX = 25
#else
    RAIDER_20 = 20,
    RAIDER_MAX = 40
#endif
};

enum class GuilderType : uint8
{
    SOLO = 0,
    TINY = 30,
    SMALL = 50,
    MEDIUM = 70,
    LARGE = 120,
    MASSIVE = 250
};

enum class ActivePiorityType : uint8
{
    IS_REAL_PLAYER = 0,
    HAS_REAL_PLAYER_MASTER = 1,
    IN_GROUP_WITH_REAL_PLAYER,
    IN_BATTLEGROUND,
    IN_INSTANCE,
    VISIBLE_FOR_PLAYER,
    IS_ALWAYS_ACTIVE,
    IN_COMBAT,
    IN_BG_QUEUE,
    IN_LFG,
    NEARBY_PLAYER,
    PLAYER_FRIEND,
    PLAYER_GUILD,
    IN_ACTIVE_AREA,
    IN_ACTIVE_MAP,
    IN_INACTIVE_MAP,
    IN_EMPTY_SERVER,
    MAX_TYPE
};

enum ActivityType
{
    GRIND_ACTIVITY = 1,
    RPG_ACTIVITY = 2,
    TRAVEL_ACTIVITY = 3,
    OUT_OF_PARTY_ACTIVITY = 4,
    PACKET_ACTIVITY = 5,
    DETAILED_MOVE_ACTIVITY = 6,
    PARTY_ACTIVITY = 7,
    REACT_ACTIVITY = 8,
    ALL_ACTIVITY = 9,
    MAX_ACTIVITY_TYPE
};

enum BotRoles
{
    BOT_ROLE_NONE = 0x00,
    BOT_ROLE_TANK = 0x01,
    BOT_ROLE_HEALER = 0x02,
    BOT_ROLE_DPS = 0x04
};

class ChatCommandHolder
{
public:
    ChatCommandHolder(std::string command, Player* owner = NULL, uint32 type = CHAT_MSG_WHISPER, time_t time = 0) : command(command), owner(owner), type(type), time(time) {}
    ChatCommandHolder(ChatCommandHolder const& other)
    {
        this->command = other.command;
        this->owner = other.owner;
        this->type = other.type;
        this->time = other.time;
    }

public:
    std::string GetCommand() { return command; }
    Player* GetOwner() { return owner; }
    uint32 GetType() { return type; }
    time_t GetTime() { return time; }

private:
    std::string command;
    Player* owner;
    uint32 type;
    time_t time;
};

// the bot got SMSG_TRADE_STATUS (TradeStatus): its trade, as the client of a player sees it
DECLARE_MULTICAST_DELEGATE_OneParam(FOnBotTradeStatus, uint32 /*status*/);

class PlayerbotAI : public PlayerbotAIBase
{
    // reads the StateMachine and queues switches on the bot's update (OnBotUpdate)
    friend class ai::PlayerbotDebugWindow;
public:
	PlayerbotAI();
	PlayerbotAI(Player* bot);
	virtual ~PlayerbotAI();

    virtual void UpdateAI(uint32 elapsed, bool minimal = false);

    void HandleCommands();
    // chat commands waiting for HandleCommands (also delayed ones)
    bool HasPendingCommands() const { return !chatCommands.empty(); }
//private:
    virtual bool UpdateAIInternal(uint32 elapsed, bool minimal = false) override;

public:
	std::string HandleRemoteCommand(std::string command);
    void SendStateTreeDebug();
    // applies the addon's switches (StateMachine::SetDebugSwitch)
    void ApplyStateTreeToggles();
    // WoWAddonInspectBot addon: runs its equip / unequip requests, then answers its inventory request
    void ApplyInventoryCommands();
    void SendInventory();
    // runs the "rti" chat commands (set / show the kill order and crowd control icons)
    void ApplyRtiCommands();
    // WoWAddonInspectBot: applies the premade spec picked in its spec box, then answers with the inventory
    void ApplySpecCommands();
    // WoWAddonInspectBot's totem boxes: selects a totem for the set in use (outdoor or the stage's), then answers with the inventory
    void ApplyTotemCommands();
    void HandleCommand(uint32 type, const std::string& text, Player& fromPlayer, const uint32 lang = LANG_UNIVERSAL);
    void QueueChatResponse(uint32 msgType, ObjectGuid guid1, ObjectGuid guid2, std::string message, std::string chanName, std::string name, bool noDelay = false);
	void HandleBotOutgoingPacket(const WorldPacket& packet);
    // rtscAnySelected: one of the master's bots is rtsc selected (PlayerbotMgr, once per packet for every bot)
    void HandleMasterIncomingPacket(const WorldPacket& packet, bool rtscAnySelected = false);
    bool IsRtscSelected() const { return rtsc.IsSelected(); }
    void HandleMasterOutgoingPacket(const WorldPacket& packet);
	void HandleTeleportAck();
    // what the client does for a player: entering a teleport area trigger (dungeon portal) sends CMSG_AREATRIGGER,
    // the core checks the requirements and teleports the bot like a player
    void UpdateAreaTriggers();
    // the trade status packets the bot gets (UIInteractionProvider_GlobalTask); declared before StateMachine, which
    // unsubscribes from it when its trees are destroyed
    FOnBotTradeStatus onTradeStatus;
    void Reset(bool full = false);
    bool IsTank(Player* player, bool inGroup = true);
    bool IsHeal(Player* player, bool inGroup = true);
    bool IsRanged(Player* player, bool inGroup = true);
    Creature* GetCreature(ObjectGuid guid) const;
    Creature* GetAnyTypeCreature(ObjectGuid guid) const;
    Unit* GetUnit(ObjectGuid guid);
    static Unit* GetUnit(CreatureDataPair const* creatureDataPair);
    GameObject* GetGameObject(ObjectGuid guid);
    static GameObject* GetGameObject(GameObjectDataPair const* gameObjectDataPair);
    WorldObject* GetWorldObject(ObjectGuid guid);
    std::vector<Player*> GetPlayersInGroup();
    void DropQuest(uint32 questId);
    std::vector<const Quest*> GetAllCurrentQuests();
    std::vector<const Quest*> GetCurrentIncompleteQuests();
    std::set<uint32> GetAllCurrentQuestIds();
    std::set<uint32> GetCurrentIncompleteQuestIds();
    const Quest* GetCurrentIncompleteQuestWithId(uint32 questId);
    bool HasCurrentIncompleteQuestWithId(uint32 questId);
    std::vector<std::pair<const Quest*, uint32>> GetCurrentQuestsRequiringItemId(uint32 itemId);
    const AreaTableEntry* GetCurrentArea();
    const AreaTableEntry* GetCurrentZone();
    std::string GetLocalizedAreaName(const AreaTableEntry* entry);
    bool IsInCapitalCity();
    ChatChannelSource GetChatChannelSource(Player* bot, uint32 type, std::string channelName);
    bool SayToGuild(std::string msg, bool likePlayer = false);
    bool SayToWorld(std::string msg);
    bool SayToGeneral(std::string msg);
    bool SayToTrade(std::string msg);
    bool SayToLFG(std::string msg);
    bool SayToLocalDefense(std::string msg);
    bool SayToWorldDefense(std::string msg);
    bool SayToGuildRecruitment(std::string msg);
    bool SayToParty(std::string msg, bool likePlayer = false);
    bool SayToRaid(std::string msg);
    bool Yell(std::string msg, bool likePlayer = false);
    bool Say(std::string msg, bool likePlayer = false);
    bool Whisper(std::string msg, std::string receiverName, bool likePlayer = false);
    bool TellPlayer(Player* player, std::ostringstream &stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false) { return TellPlayer(player, stream.str(), securityLevel, isPrivate, ignoreSilent); }
    bool TellPlayer(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool ignoreSilent = false);
    bool TellPlayerNoFacing(Player* player, std::ostringstream& stream, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false) { return TellPlayerNoFacing(player, stream.str(), securityLevel, isPrivate, noRepeat, ignoreSilent); }
    bool TellPlayerNoFacing(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool isPrivate = true, bool noRepeat = true, bool ignoreSilent = false);
    bool TellError(Player* player, std::string text, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL, bool ignoreSilent = false);
    void SpellInterrupted(uint32 spellid);

    // the bot's own last SMSG_SPELL_GO (the spell went off) and SMSG_SPELL_FAILURE (it was interrupted / failed),
    // WorldTimer ms; spellId 0 = none yet
    struct SpellCastEvent
    {
        uint32 spellId = 0;
        uint32 ms = 0;
    };
    const SpellCastEvent& GetLastSpellGo() const { return lastSpellGo; }
    const SpellCastEvent& GetLastSpellFailure() const { return lastSpellFailure; }
    int32 CalculateGlobalCooldown(uint32 spellid);
    void InterruptSpell(bool withMeleeAndAuto = true);
    bool RemoveAura(const std::string& name);
    void RemoveShapeshift();
    void WaitForSpellCast(Spell *spell);
    bool PlaySound(uint32 emote);
    bool PlayEmote(uint32 emote);
    bool PlayAttackEmote(float chanceDivider);
    void Ping(float x, float y);
    void Poi(float x, float y, std::string icon_name = "This way", Player* player = nullptr, uint32 flags = 99, uint32 icon = 6 /* red flag */, uint32 icon_data = 0);
    Item * FindPoison() const;
    Item * FindConsumable(uint32 displayId) const;
    Item * FindBandage() const;
    Item* FindStoneFor(Item* weapon) const;
    Item* FindOilFor(Item* weapon) const;
#ifdef MANGOSBOT_ZERO
    void ImbueItem(Item* item, uint16 targetFlag, ObjectGuid targetGUID);
#else
    void ImbueItem(Item* item, uint32 targetFlag, ObjectGuid targetGUID);
#endif
    void ImbueItem(Item* item, uint8 targetInventorySlot);
    void ImbueItem(Item* item, Unit* target);
    void ImbueItem(Item* item);
    void EnchantItemT(uint32 spellid, uint8 slot, Item* item = nullptr);
    uint32 GetBuffedCount(Player* player, std::string spellname);

    bool GetSpellRange(std::string name, float* maxRange, float* minRange = nullptr);
    uint32 GetSpellCastDuration(Spell* spell);

    virtual bool HasAura(std::string spellName, Unit* player, bool maxStack = false, bool checkIsOwner = false, int maxAmount = -1, bool hasMyAura = false, int minDuration = 0, int auraTypeId = TOTAL_AURAS);
    virtual bool HasAnyAuraOf(Unit* player, ...);
    virtual bool HasMyAura(std::string spellName, Unit* player) { return HasAura(spellName, player, false, false, -1, true); }
    uint8 GetHealthPercent(const Unit& target) const;
    uint8 GetHealthPercent() const;
    uint8 GetManaPercent(const Unit& target) const;
    uint8 GetManaPercent() const;

    virtual bool IsInterruptableSpellCasting(Unit* player, std::string spell, uint8 effectMask);
    virtual bool HasAuraToDispel(Unit* player, uint32 dispelType);
    bool canDispel(const SpellEntry* entry, uint32 dispelType);
    static bool IsHealSpell(const SpellEntry* entry);

    static bool IsMiningNode(const GameObject* go);
    static bool IsHerb(const GameObject* go);

    bool HasSpell(std::string name) const;
    bool HasSpell(uint32 spellid) const;
    // the bot's (or its pet's) highest rank of the spell: a name, a spell link or a chat spell name (was the legacy
    // "spell id" value), 0 when it has none
    uint32 GetSpellIdByName(const std::string& name) const;
    // the item a spell is cast on (enchants, poisons, ...: trade slot, equipped, bags), nullptr when none (was the
    // legacy "item for spell" value)
    Item* GetItemForSpell(uint32 spellId) const;
    bool HasAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(uint32 spellId, Unit* player, bool checkOwner = false);
    Aura* GetAura(std::string spellName, Unit* player, bool checkOwner = false);
    std::vector<Aura*> GetAuras(Unit* player, bool allAuras = true, bool positive = false);

    bool HasSpellItems(uint32 spellId, const Item* castItem) const;
    void DurabilityLoss(Item* item, double percent);

    virtual bool CanCastSpell(std::string name, Unit* target, uint8 effectMask, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, GameObject* goTarget, uint8 effectMask, bool checkHasSpell = true, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastSpell(uint32 spellid, float x, float y, float z, uint8 effectMask, bool checkHasSpell = true, Item* itemTarget = nullptr, bool ignoreRange = false, bool ignoreInCombat = false, bool ignoreMount = false, SpellCastResult* checkResult = nullptr);
    bool CanCastVehicleSpell(uint32 spellid, Unit* target);

    virtual bool CastSpell(std::string name, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, Unit* target, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, GameObject* goTarget, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    bool CastSpell(uint32 spellId, float x, float y, float z, Item* itemTarget = nullptr, bool waitForSpell = true, uint32* outSpellDuration = nullptr);
    // casts a spell at a corpse, as a client targeting the body does (a resurrection of a released player, whose
    // ghost may be on another map); false when it did not start
    bool CastSpellOnCorpse(uint32 spellId, Corpse* corpse);
    bool CastPetSpell(uint32 spellId, Unit* target);
    bool CastVehicleSpell(uint32 spellId, Unit* target, float projectileSpeed, bool needTurn);
    bool CastVehicleSpell(uint32 spellId, float x, float y, float z);

    bool IsInVehicle(bool canControl = false, bool canCast = false, bool canAttack = false, bool canTurn = false, bool fixed = false, std::string vehicleName = "");

    uint32 GetEquipGearScore(Player* player, bool withBags, bool withBank);
    uint32 GetEquipStatsValue(Player* player);
    bool HasSkill(SkillType skill);
    bool IsAllowedCommand(std::string text);
    float GetRange(std::string type);

    static ReputationRank GetFactionReaction(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate);
    static bool friendToAlliance(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sFactionTemplateStore.LookupEntry(1)) >= REP_NEUTRAL; }
    static bool friendToHorde(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(templateEntry, sFactionTemplateStore.LookupEntry(2)) >= REP_NEUTRAL; }
    bool IsFriendlyTo(FactionTemplateEntry const* templateEntry) { return GetFactionReaction(bot->GetFactionTemplateEntry(), templateEntry) >= REP_NEUTRAL; }
    bool IsFriendlyTo(uint32 faction) { return GetFactionReaction(bot->GetFactionTemplateEntry(), sFactionTemplateStore.LookupEntry(faction)) >= REP_NEUTRAL; }
    static bool AddAura(Unit* unit, uint32 spellId);
    ReputationRank getReaction(FactionTemplateEntry const* factionTemplate) { return GetFactionReaction(bot->GetFactionTemplateEntry(), factionTemplate);}

    void InventoryIterateItems(IterateItemsVisitor* visitor, IterateItemsMask mask);
    void InventoryTellItems(Player* player, std::map<uint32, int> items, std::map<uint32, bool> soulbound);
    void InventoryTellItem(Player* player, ItemPrototype const* proto, int count, bool soulbound);
    std::list<Item*> InventoryParseItems(std::string text, IterateItemsMask mask);
    uint32 InventoryGetItemCount(FindItemVisitor* visitor, IterateItemsMask mask);
    std::string InventoryParseOutfitName(std::string outfit);
    ItemIds InventoryParseOutfitItems(std::string outfit);
    ItemIds InventoryFindOutfitItems(std::string name);

    void AccelerateRespawn(Creature* creature, float accelMod = 0);
    void AccelerateRespawn(ObjectGuid guid, float accelMod = 0) { Creature* creature = GetCreature(guid); if (creature) AccelerateRespawn(creature,accelMod); }

    std::list<Unit*> GetAllHostileUnitsAroundWO(WorldObject* wo, float distanceAround);
    std::list<Unit*> GetAllHostileNPCNonPetUnitsAroundWO(WorldObject* wo, float distanceAround);

public:
    std::vector<Bag*> GetEquippedAnyBags();
    std::vector<Bag*> GetEquippedQuivers();
    std::vector<Item*> GetInventoryAndEquippedItems();
    std::vector<Item*> GetInventoryItems();
    uint32 GetInventoryItemsCountWithId(uint32 itemId);
    bool HasItemInInventory(uint32 itemId);
    bool HasNotFullStacksInBagsForLootItems(LootItemList &questLootItemList);
    bool HasQuestItemsInLootList(LootItemList &questLootItemList);
    bool HasQuestItemsInWOLootList(WorldObject* wo);
    bool CanLootSomethingFromWO(WorldObject* wo);

private:
    void InventoryIterateItemsInBags(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInEquip(IterateItemsVisitor* visitor);   
    void InventoryIterateItemsInBank(IterateItemsVisitor* visitor);
    void InventoryIterateItemsInBuyBack(IterateItemsVisitor* visitor);

private:
    void _fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore);
    bool IsTellAllowed(Player* player, PlayerbotSecurityLevel securityLevel = PlayerbotSecurityLevel::PLAYERBOT_SECURITY_ALLOW_ALL);

public:
	Player* GetBot() { return bot; }
    Player* GetMaster() { return master; }

    //Checks if the bot is really a player. Players always have themselves as master.
    bool IsRealPlayer() { return bot->GetSession()->GetRemoteAddress() != "disconnected/bot"; }
    bool IsRealPlayer(Unit* unit) { return unit->IsPlayer() && ((Player*)unit)->GetSession()->GetRemoteAddress() != "disconnected/bot"; }
    bool IsSelfMaster() { return master ? (master == bot) : false; }
    //Bot has a master that is a player.
    bool HasRealPlayerMaster() { return master && (!master->GetPlayerbotAI() || master->GetPlayerbotAI()->IsRealPlayer()); } 
    //Bot has a master that is actively playing.
    bool HasActivePlayerMaster() { return master && !master->GetPlayerbotAI(); }
    //Checks if the bot is summoned as alt of a player
    bool IsAlt() { return HasRealPlayerMaster() && !sRandomPlayerbotMgr.IsRandomBot(bot); }
    //Get the group leader or the master of the bot.
    Player* GetGroupMaster() { return bot->InBattleGround() ? master : bot->GetGroup() ? (sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid()) ? sObjectMgr.GetPlayer(bot->GetGroup()->GetLeaderGuid()) : master) : master; }

    bool IsGroupLeader() { return bot->GetGroup() && bot->GetGroup()->GetLeaderGuid() == bot->GetObjectGuid(); }

    //Check if player is safe to use.
    bool IsSafe(Player* player) { return player && player->GetMapId() == bot->GetMapId() && player->GetInstanceId() == bot->GetInstanceId() && !player->IsBeingTeleported(); }
    // in a dungeon with stages: the bot must not go to the point, a stage there is not cleared
    // (InstanceSubsystem::IsEntryBlocked: out of combat, or in combat for boxes that can not be entered)
    bool IsStageEntryBlocked(float x, float y, float z) const { return IsStageEntryBlocked(x, y, z, bot->IsInCombat()); }
    // the same with the combat state given: inCombat lets the bot into the boxes that may be entered in combat (melee
    // after a unit its group fights), false blocks every box of a stage that is not cleared (charges)
    bool IsStageEntryBlocked(float x, float y, float z, bool inCombat) const;
    bool IsSafe(WorldObject* obj) { return obj && obj->GetMapId() == bot->GetMapId() && obj->GetInstanceId() == bot->GetInstanceId() && (!obj->IsPlayer() || !((Player*)obj)->IsBeingTeleported()); }

    //Returns a semi-random (cycling) number that is fixed for each bot.
    uint32 GetFixedBotNumber(BotTypeNumber typeNumber, uint32 maxNum = 100, float cyclePerMin = 1, bool ignoreGuid = false); 

    GrouperType GetGrouperType();
    GuilderType GetGuilderType();
    uint32 GetMaxPreferedGuildSize();

    bool HasPlayerNearby(WorldPosition pos, float range);
    bool HasPlayerNearby(float range = sPlayerbotAIConfig.reactDistance);
    bool HasManyPlayersNearby(uint32 trigerrValue = 20, float range = sPlayerbotAIConfig.sightDistance);
    bool ChannelHasRealPlayer(std::string channelName);


    ActivePiorityType GetPriorityType();
    std::pair<uint32,uint32> GetPriorityBracket(ActivePiorityType type);
    bool AllowActive(ActivityType activityType);
    bool AllowActivity(ActivityType activityType = ALL_ACTIVITY, bool checkNow = false);

    bool HasCheat(BotCheatMask mask) const;
    BotCheatMask GetCheat() { return cheatMask; }
    void SetCheat(BotCheatMask mask) { cheatMask = mask; }

    void SetMaster(Player* master) { this->master = master; }
    ChatHelper* GetChatHelper() { return &chatHelper; }
    PerformanceStack* GetPerformanceStack() { return &performanceStack; }
    bool IsOpposing(Player* player);
    static bool IsOpposing(uint8 race1, uint8 race2);
    PlayerbotSecurity* GetSecurity() { return &security; }
    // where the bot stands in dungeon stages (rtsc overrides, ai_playerbot_dungeon_positions)
    ai::PlayerbotDungeonPositions& GetDungeonPositions() { return dungeonPositions; }
    // where the bot last stood outside every stage that is not cleared, other than its master's
    // (InstanceProvider_GlobalTask, every 500ms of any tree): its way back out of such a stage it got into
    struct StageWayBack
    {
        uint32 mapId = 0;
        uint32 instanceId = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool valid = false;
    };
    StageWayBack& GetStageWayBack() { return stageWayBack; }
    // how far the bot walked the path of its master's active plan pull (InstanceProvider_GlobalTask): the next
    // waypoint; started at the waypoint nearest to the bot when the pull became active
    struct PlanPathProgress
    {
        uint32 mapId = 0;
        uint32 instanceId = 0;
        uint32 pullId = 0;
        uint32 index = 0;
        bool valid = false;
    };
    PlanPathProgress& GetPlanPathProgress() { return planPathProgress; }
    // the raid target icons the bot attacks / crowd-controls (rti / rti cc, ai_playerbot_db_store)
    ai::PlayerbotTargetIcons& GetTargetIcons() { return targetIcons; }
    // the totems a shaman keeps up, outdoor and per dungeon stage (ai_playerbot_totems)
    ai::PlayerbotTotems& GetTotems() { return totems; }
    // the bot's movement: drives the trees' MovementGoal_GlobalTask goals (the only code that moves the bot)
    ai::PlayerbotMovement& GetMovement() { return movement; }

    WorldPosition GetJumpDestination() { return jumpDestination; }
    void SetJumpDestination(const WorldPosition& pos) { jumpDestination = pos; }
    void ResetJumpDestination() { jumpDestination = WorldPosition(); }

    bool IsJumping() { return jumpTime; }
    void SetFallAfterJump() { fallAfterJump = true; }
    void SetJumpTime(uint32 time) { jumpTime = time; }
    bool CanMove();
    void StopMoving();
    bool IsInRealGuild();
    void SetPlayerFriend(bool isFriend) {isPlayerFriend = isFriend;}
    bool IsPlayerFriend() { return isPlayerFriend; }
    bool HasPlayerRelation();

    // the state machine's main state is type (NonCombat, Combat or Dead)
    bool IsInStateType(HLSStateType type) const;
    time_t GetCombatStartTime() const { return combatStartTime; }

    void OnCombatStarted();
    void OnCombatEnded();
    void OnDeath();
    void OnResurrected();

    // kept up to date by TeamSubsystem events ("has attackers", "attackers count"):
    // units that have the bot, its pet or a group member on the same map (any distance) on their threat list
    uint32 GetTeamAttackerCount() const { return uint32(teamAttackers.size()); }
    // attacker -> victim (the bot or a member) -> links (threat list entry, melee attacker) to it and its pet
    const std::map<ObjectGuid, std::map<ObjectGuid, uint32>>& GetTeamAttackers() const { return teamAttackers; }
    // changes whenever GetTeamAttackers does ("attackers" rebuilds its list only then)
    uint32 GetTeamAttackersVersion() const { return teamAttackersVersion; }

    // the bot's health / power percent conditions ("low health", "low mana", ...), kept by its change broadcasts
    UnitResourceThresholds& GetSelfThresholds() { return selfThresholds; }
    // the bot or a group member near it is in combat, or the bot announced a fight (pull, attack), or it has attackers
    bool IsTeamFighting() const { return teamInCombat || !teamAttackers.empty(); }
private:
    // TeamSubsystem delegates of the bot, bound in the constructor: keep teamInCombat / teamAttackers and call the
    // On* handlers above when the state machine's state no longer matches them
    void HandleCombatStarted(ObjectGuid self);
    void HandleMemberCombatStarted(ObjectGuid self, Player* member);
    void HandleCombatEnded(ObjectGuid self);
    void HandleMemberCombatEnded(ObjectGuid self, Player* member);
    void HandleAttackerAdded(ObjectGuid self, Unit* attacker);
    void HandleMemberAttackerAdded(ObjectGuid self, Player* member, Unit* attacker);
    void HandleAttackerRemoved(ObjectGuid self, Unit* attacker);
    void HandleMemberAttackerRemoved(ObjectGuid self, Player* member, Unit* attacker);
    void HandleDied(ObjectGuid self);
    void HandleResurrected(ObjectGuid self);
    // group changes come from the world thread: only flagged there, RebuildTeamCombat runs in the bot's next UpdateAI
    void HandleTeamChanged(Group* /*group*/, ObjectGuid /*self*/) { teamChanged = true; }
    void HandleMemberTeamChanged(Group* /*group*/, ObjectGuid /*self*/, ObjectGuid /*member*/) { teamChanged = true; }
    // a teleport leaves the members' map: their attacker events stop reaching the bot
    void HandleEnterMap(Player* /*player*/, Map* /*map*/) { teamChanged = true; }
    // member is alive and within sight distance of the bot (whose combat starts / ends count for the bot)
    bool IsNearbyMember(Player* member) const;
    // the bot or a nearby group member is in combat
    bool IsTeamInCombatNearby() const;
    // teamInCombat (nearby members) and teamAttackers (members on the map) read from the core (login, group changes)
    void RebuildTeamCombat();
    // one threat link of attacker to victim (the bot or a member, their pets count for them)
    void AddTeamAttacker(Unit* attacker, ObjectGuid victim);
    void RemoveTeamAttacker(Unit* attacker, ObjectGuid victim);
    // OnCombatStarted / OnCombatEnded when IsTeamFighting no longer matches the state machine's state
    void UpdateCombatState();
public:
    
    void SetActionDuration(uint32 duration);

    // afk, master / group upkeep and stand state, every update after the state machine
    void UpdateUpkeep();

    bool IsImmuneToSpell(uint32 spellId) const;

    bool IsInPve();
    bool IsInPvp();
    bool IsInRaid();

    void SetMoveToTransport(bool flag = true) { isMovingToTransport = flag; }
    bool GetMoveToTransport() { return isMovingToTransport; }

    void SetShouldLogOut(bool val = true) { shouldLogOut = val; }
    bool GetShouldLogOut() { return shouldLogOut; }

    PlayerTalentSpec GetTalentSpec();
    void UpdateTalentSpec(PlayerTalentSpec spec = PlayerTalentSpec::TALENT_SPEC_INVALID);

    bool CanEnterArea(const AreaTrigger* area);
    void Unmount();

    void QueuePacket(WorldPacket& pkt);

    float GetLevelFloat() const;

    static bool CanSpellClick(Player* bot, uint32 entry);
    bool CanSpellClick(ObjectGuid guid) const;
    bool HandleSpellClick(uint32 entry);
    bool HandleSpellClick(ObjectGuid guid);

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> GetWeakPtr() const { return m_weakRef; }
    void SetWeakPtr(MaNGOS::unique_weak_ptr<PlayerbotAI> weakRef) { m_weakRef = std::move(weakRef); }
#endif

//private:

protected:
	Player* bot = nullptr;
	Player* master = nullptr;
	uint32 accountId = 0;
protected:
    ChatHelper chatHelper;
    std::queue<ChatCommandHolder> chatCommands;
    std::queue<ChatQueuedReply> chatReplies;
    CompositeChatFilter chatFilter;
    PlayerbotSecurity security;
    std::map<std::string, time_t> whispers;
    std::pair<ChatMsg, time_t> currentChat;
    static std::set<std::string> unsecuredCommands;
    bool allowActive[MAX_ACTIVITY_TYPE];
    time_t allowActiveCheckTimer[MAX_ACTIVITY_TYPE];
    bool inCombat = false;
    bool isMoving = false;
    bool isWaiting = false;
    uint32 insideAreaTrigger = 0;   // the teleport area trigger the bot stands in (fires once when entered)
    BotCheatMask cheatMask = BotCheatMask::none;
    WorldPosition jumpDestination;
    uint32 jumpTime = 0;
    bool fallAfterJump = false;
    uint32 faceTargetUpdateDelay = 0;
    bool isPlayerFriend = false;
    bool isMovingToTransport = false;
    bool shouldLogOut = false;
    // the performance monitor's operations of this bot (PerformanceMonitor::start)
    PerformanceStack performanceStack;
    // when the combat started (0 out of combat)
    time_t combatStartTime = 0;
    PlayerTalentSpec talentSpec = PlayerTalentSpec::TALENT_SPEC_INVALID;

    StateMachine StateMachine;

    // attached to the bot in the constructor (the bot outlives this object: it owns it)
    UnitResourceThresholds selfThresholds;

    // see IsTeamFighting / GetTeamAttackerCount
    bool teamInCombat = false;
    // attacker -> victim (the bot or a nearby member) -> threat links to it and its pet
    std::map<ObjectGuid, std::map<ObjectGuid, uint32>> teamAttackers;
    uint32 teamAttackersVersion = 0;
    std::atomic<bool> teamChanged{ true };          // rebuild in the next UpdateAI (also the first one)

    // player waiting for a StateTree snapshot ("BOT\tstatetree" addon request); set from chat, consumed in UpdateAI
    std::atomic<uint64> stateTreeDebugRequester{ 0 };
    std::atomic<bool> stateTreeDebugHistory{ false };
    // task / node / condition switches of the BotStateTree addon ("BOT\tstatetree toggle|nodetoggle|condition ..."):
    // set from chat, applied in UpdateAI
    struct StateTreeToggle
    {
        StateTreeSwitch what = StateTreeSwitch::Task;
        std::string state, tree;
        int node = -1, index = -1;
        int value = 0;
    };
    std::mutex stateTreeToggleLock;
    std::vector<StateTreeToggle> stateTreeToggles;
    uint8 stateTreeDebugSeq = 0;
    // last listing / view / history copy for the server's debug window (PlayerbotDebugWindow::OnBotUpdate)
    uint32 debugWindowListedMs = 0, debugWindowViewMs = 0, debugWindowHistoryMs = 0;
    // player waiting for the bot's gear and bags ("BOT\tinventory" addon request of WoWAddonInspectBot) and the
    // addon's equip / unequip requests ("BOT\tequip <bag> <slot> <inventory slot>", "BOT\tunequip <inventory slot>",
    // client numbering: bags 0-4, slots from 1, inventory slots 1-19); set from chat, run in UpdateAI
    std::atomic<uint64> inventoryRequester{ 0 };
    struct InventoryCommand
    {
        bool equip = true;
        int bag = 0, slot = 0;      // equip: the item in the bags
        int inventorySlot = -1;     // equip: where to (-1 wherever it fits), unequip: what
        uint64 requester = 0;
    };
    std::mutex inventoryCommandLock;
    std::vector<InventoryCommand> inventoryCommands;
    uint8 inventorySeq = 0;
    ai::PlayerbotDungeonPositions dungeonPositions;
    StageWayBack stageWayBack;
    PlanPathProgress planPathProgress;
    ai::PlayerbotTargetIcons targetIcons;
    ai::PlayerbotTotems totems;
    ai::PlayerbotMovement movement;
    // RTS control: "rtsc ..." commands and the master's Aedm casts, run in UpdateAI
    ai::PlayerbotRtsc rtsc;
    // "rti ..." chat commands (the text after "rti", the player to answer): set from chat, run in UpdateAI
    struct RtiCommand
    {
        std::string text;
        uint64 requester = 0;
        bool addon = false;         // WoWAddonInspectBot: no whisper, the addon gets the bot's inventory (and icons)
    };
    // WoWAddonInspectBot's spec box ("BOT\tspec <premade spec id>"): set from chat, run in UpdateAI
    struct SpecCommand
    {
        int pathId = -1;
        uint64 requester = 0;
    };
    std::mutex specCommandLock;
    std::vector<SpecCommand> specCommands;
    // WoWAddonInspectBot's totem boxes ("BOT	totem <school> <spell id, 0 none>"): set from chat, run in UpdateAI
    struct TotemCommand
    {
        std::string school;
        uint32 spellId = 0;
        uint64 requester = 0;
    };
    std::mutex totemCommandLock;
    std::vector<TotemCommand> totemCommands;
    std::mutex rtiCommandLock;
    std::vector<RtiCommand> rtiCommands;

#ifdef BUILD_ELUNA
    MaNGOS::unique_weak_ptr<PlayerbotAI> m_weakRef;
#endif
private:
    SpellCastEvent lastSpellGo;
    SpellCastEvent lastSpellFailure;
};
