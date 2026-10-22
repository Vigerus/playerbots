#pragma once

#include "Loot/LootMgr.h"
#include "Globals/ObjectMgr.h"
#include "Globals/ObjectAccessor.h"

#include <unordered_map>
#include <vector>

// Read access to the core's loot templates and vendor lists (was part of the legacy loot / vendor values, see
// deprecated/playerbot/strategy/values/LootValues.* and VendorValues.*)
namespace ai
{
    //Cheat class copy to hack into the loot system
    class LootLootGroupAccess                               // A set of loot definitions for items (refs are not allowed)
    {
    public:
        LootStoreItemList ExplicitlyChanced;                // Entries with chances defined in DB
        LootStoreItemList EqualChanced;                     // Zero chances - every entry takes the same chance
    };

    class LootTemplateAccess
    {
    public:
        typedef std::vector<LootLootGroupAccess> LootGroups;
        LootStoreItemList Entries;                          // not grouped only
        LootGroups        Groups;                           // groups have own (optimized) processing, grouped entries go there
    };

    class LootAccess
    {
    public:
        friend struct LootItem;
        friend class GroupLootRoll;
        friend class LootMgr;

        std::vector<LootItem*> GetLootContentFor(Player* player) const;
        uint32 GetLootStatusFor(Player const* player) const;
        bool IsLootedFor(Player const* player) const;
        bool IsLootedForAll() const;

        // What is looted
        WorldObject* m_lootTarget;
        Item* m_itemTarget;
        ObjectGuid       m_guidTarget;

        LootItemList     m_lootItems;                     // store of the items contained in loot
        uint32           m_gold;                          // amount of money contained in loot
        uint32           m_maxSlot;                       // used to increment slot index and get total items count
        LootType         m_lootType;                      // internal loot type
        ClientLootType   m_clientLootType;                // client loot type
        LootMethod       m_lootMethod;                    // used to know what kind of check must be done at loot time
        ItemQualities    m_threshold;                     // group threshold for items
        ObjectGuid       m_masterOwnerGuid;               // master loot player or round robin owner
        ObjectGuid       m_currentLooterGuid;             // current player for under threshold items (Round Robin)
        GuidSet          m_ownerSet;                      // set of all player who have right to the loot
        uint32           m_maxEnchantSkill;               // used to know group right to use disenchant option
        bool             m_haveItemOverThreshold;         // if at least one item in the loot is over threshold
        bool             m_isChecked;                     // true if at least one player received the loot content
        bool             m_isChest;                       // chest type object have special loot right
        bool             m_isChanged;                     // true if at least one item is looted
        bool             m_isFakeLoot;                    // nothing to loot but will sparkle for empty windows
        GroupLootRollMap m_roll;                          // used if an item is under rolling
        GuidSet          m_playersLooting;                // player who opened loot windows
        GuidSet          m_playersOpened;                 // players that have released the corpse
        TimePoint        m_createTime;                    // create time (used to refill loot if need)
    };

    inline std::vector<LootItem*> LootAccess::GetLootContentFor(Player* player) const
    {
        std::vector<LootItem*> retvec;

        for (LootItemList::const_iterator lootItemItr = m_lootItems.begin(); lootItemItr != m_lootItems.end(); ++lootItemItr)
        {
            retvec.push_back(*lootItemItr);
        }

        return retvec;
    }

    // Get loot status for a specified player
    inline uint32 LootAccess::GetLootStatusFor(Player const* player) const
    {
        uint32 status = 0;

        if (m_isFakeLoot && m_playersOpened.empty())
            return LOOT_STATUS_FAKE_LOOT;

        if (m_gold != 0)
            status |= LOOT_STATUS_CONTAIN_GOLD;

        for (auto lootItem : m_lootItems)
        {
            Loot const* loot = reinterpret_cast<Loot const*>(this);
            LootSlotType slotType = lootItem->GetSlotTypeForSharedLoot(player, loot);
            if (slotType == MAX_LOOT_SLOT_TYPE)
                continue;

            status |= LOOT_STATUS_NOT_FULLY_LOOTED;

            if (lootItem->freeForAll)
                status |= LOOT_STATUS_CONTAIN_FFA;

            if (lootItem->isReleased)
                status |= LOOT_STATUS_CONTAIN_RELEASED_ITEMS;
        }
        return status;
    }

    // Is there is any loot available for provided player
    inline bool LootAccess::IsLootedFor(Player const* player) const
    {
        return (GetLootStatusFor(player) == 0);
    }

    inline bool LootAccess::IsLootedForAll() const
    {
        for (auto itr : m_ownerSet)
        {
            Player* player = ObjectAccessor::FindPlayer(itr);
            if (!player)
                continue;

            if (!IsLootedFor(player))
                return false;
        }
        return true;
    }

    //DropMap[itemId] = {entry}
    typedef std::unordered_multimap<uint32, int32> DropMap;
    //VendorMap[itemId] = {entry}
    typedef std::unordered_multimap<uint32, int32> VendorMap;

    inline LootTemplateAccess const* GetLootTemplate(ObjectGuid guid, LootType type = LOOT_CORPSE)
    {
    	LootTemplate const* lTemplate = nullptr;

    	if (guid.IsCreature())
    	{
    		CreatureInfo const* info = sObjectMgr.GetCreatureTemplate(guid.GetEntry());

    		if (info)
    		{
    			if (type == LOOT_CORPSE)
    				lTemplate = LootTemplates_Creature.GetLootFor(info->LootId);
    			else if (type == LOOT_PICKPOCKETING && info->PickpocketLootId)
    				lTemplate = LootTemplates_Pickpocketing.GetLootFor(info->PickpocketLootId);
    			else if (type == LOOT_SKINNING && info->SkinningLootId)
    				lTemplate = LootTemplates_Skinning.GetLootFor(info->SkinningLootId);
    		}
    	}
    	else if (guid.IsGameObject())
    	{
    		GameObjectInfo const* info = sObjectMgr.GetGameObjectInfo(guid.GetEntry());

    		if (info && info->GetLootId() != 0)
    		{
    			if (type == LOOT_CORPSE)
    				lTemplate = LootTemplates_Gameobject.GetLootFor(info->GetLootId());
    			else if (type == LOOT_FISHINGHOLE)
    				lTemplate = LootTemplates_Fishing.GetLootFor(info->GetLootId());
    		}
    	}
    	else if (guid.IsItem())
    	{
    		ItemPrototype const* proto = sObjectMgr.GetItemPrototype(guid.GetEntry());
		
    		if (proto)
    		{
    			if (type == LOOT_CORPSE)
    				lTemplate = LootTemplates_Item.GetLootFor(proto->ItemId);
    			else if (type == LOOT_DISENCHANTING && proto->DisenchantID)
    				lTemplate = LootTemplates_Disenchant.GetLootFor(proto->DisenchantID);
    #ifdef MANGOSBOT_TWO
    			if (type == LOOT_MILLING)
    				lTemplate = LootTemplates_Milling.GetLootFor(proto->ItemId);
    			if (type == LOOT_PROSPECTING)
    				lTemplate = LootTemplates_Prospecting.GetLootFor(proto->ItemId);
    #endif
    		}
    	}

    	LootTemplateAccess const* lTemplateA = reinterpret_cast<LootTemplateAccess const*>(lTemplate);

    	return lTemplateA;
    }

    // vendorMap[itemId] = {creature entry} of the vendors selling the item without a limited amount
    inline VendorMap* CalculateVendorMap()
    {
        VendorMap* vendorpMap = new VendorMap;


        for (uint32 entry = 0; entry < sCreatureStorage.GetMaxEntry(); entry++)
        {
            CreatureInfo const* cInfo = sObjectMgr.GetCreatureTemplate(entry);

            if (!cInfo)
                continue;

            VendorItemList vendorItems;
            VendorItemData const* vItems = sObjectMgr.GetNpcVendorItemList(entry);

            if (vItems)
                for (auto vItem : vItems->m_items)
                    if (!vItem->maxcount) //Ignore limited amount items.
                        vendorpMap->insert(std::make_pair(vItem->item, entry));

            uint32 vendorId = cInfo->VendorTemplateId;
            if (vendorId)
            {
                vItems = sObjectMgr.GetNpcVendorTemplateItemList(vendorId);
                if (vItems)
                    for (auto vItem : vItems->m_items)
                        if (!vItem->maxcount) //Ignore limited amount items.
                            vendorpMap->insert(std::make_pair(vItem->item, entry));
            }
        }

        return vendorpMap;
    }
}
