#include "ItemEvalSubsystem.h"

#include "playerbot/RandomItemMgr.h"

#include "Entities/Bag.h"
#include "Entities/Item.h"
#include "Entities/Player.h"

#include <tuple>

namespace
{
    constexpr uint32 vendorBelowLevel = 60;

    // compared in this order: stat weight, quality, item level
    struct GearScore
    {
        uint32 weight = 0;
        uint32 quality = 0;
        uint32 itemLevel = 0;

        bool operator<(const GearScore& other) const
        {
            return std::tie(weight, quality, itemLevel) < std::tie(other.weight, other.quality, other.itemLevel);
        }
    };

    GearScore Score(Player* player, Item* item)
    {
        const ItemPrototype* proto = item->GetProto();
        return { sRandomItemMgr.ItemStatWeight(player, item), proto->Quality, proto->ItemLevel };
    }

    bool IsGenericBag(const ItemPrototype* proto)
    {
        return proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER;
    }

    bool IsWornBag(const Item* item)
    {
        return !item->IsInBag() && item->GetSlot() >= INVENTORY_SLOT_BAG_START && item->GetSlot() < INVENTORY_SLOT_BAG_END;
    }
}

const char* ToString(EItemEval eval)
{
    switch (eval)
    {
        case EItemEval::Vendor: return "Vendor";
        case EItemEval::AH:     return "AH";
        case EItemEval::Keep:   return "Keep";
        case EItemEval::Equip:  return "Equip";
    }
    return "?";
}

EItemEval ItemEvalSubsystem::Evaluate(Player* player, Item* item) const
{
    const ItemPrototype* proto = item ? item->GetProto() : nullptr;
    if (!player || !proto)
        return EItemEval::Keep;

    if (item->IsEquipped() || IsWornBag(item))
        return EItemEval::Equip;

    // TODO: soul / herb / enchanting bags for the professions and classes that fill them
    if (proto->InventoryType == INVTYPE_BAG && !IsGenericBag(proto))
        return EItemEval::Keep;

    if (proto->InventoryType == INVTYPE_NON_EQUIP)
        return proto->Quality == ITEM_QUALITY_POOR ? EvaluateWorse(player, item) : EItemEval::Keep;

    return FindEquipSlot(player, item) != NULL_SLOT ? EItemEval::Equip : EvaluateWorse(player, item);
}

uint8 ItemEvalSubsystem::FindEquipSlot(Player* player, Item* item) const
{
    const ItemPrototype* proto = item ? item->GetProto() : nullptr;
    if (!player || !proto || item->IsEquipped() || IsWornBag(item) || proto->InventoryType == INVTYPE_NON_EQUIP)
        return NULL_SLOT;

    // wrong class, armor / weapon skill or level
    if (player->CanUseItem(item, false) != EQUIP_ERR_OK)
        return NULL_SLOT;

    if (proto->InventoryType == INVTYPE_BAG)
        return IsGenericBag(proto) ? FindBagSlot(player, item) : NULL_SLOT;
    return FindGearSlot(player, item);
}

uint8 ItemEvalSubsystem::FindGearSlot(Player* player, Item* item) const
{
    const ItemPrototype* proto = item->GetProto();
    uint8 slots[4];
    if (!player->ViableEquipSlots(proto, &slots[0]))
        return NULL_SLOT;

    // the weakest of the slots it may go to is replaced
    Item* weakest = nullptr;
    for (uint8 slot : slots)
    {
        if (slot == NULL_SLOT)
            continue;

        Item* worn = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!worn)
        {
            // an empty off hand next to a two-hander is no free slot (as Player::FindEquipSlot)
            if (slot == EQUIPMENT_SLOT_OFFHAND && (player->IsTwoHandUsed() || proto->InventoryType == INVTYPE_2HWEAPON))
                continue;
            return slot;
        }

        if (!weakest || Score(player, worn) < Score(player, weakest))
            weakest = worn;
    }

    if (!weakest)
        return NULL_SLOT;

    GearScore wornScore = Score(player, weakest);
    // a two-hander takes the off hand too
    if (proto->InventoryType == INVTYPE_2HWEAPON && weakest->GetSlot() == EQUIPMENT_SLOT_MAINHAND)
        if (Item* offHand = player->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND))
            wornScore.weight += Score(player, offHand).weight;

    return wornScore < Score(player, item) ? weakest->GetSlot() : NULL_SLOT;
}

uint8 ItemEvalSubsystem::FindBagSlot(Player* player, Item* item) const
{
    // an unused bag slot, else the smallest bag smaller than this one (special bags stay)
    uint32 smallest = item->GetProto()->ContainerSlots;
    uint8 found = NULL_SLOT;
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
    {
        Item* worn = player->GetItemByPos(INVENTORY_SLOT_BAG_0, bagSlot);
        if (!worn)
            return bagSlot;
        if (IsGenericBag(worn->GetProto()) && static_cast<Bag*>(worn)->GetBagSize() < smallest)
        {
            smallest = static_cast<Bag*>(worn)->GetBagSize();
            found = bagSlot;
        }
    }
    return found;
}

EItemEval ItemEvalSubsystem::EvaluateWorse(Player* player, Item* item)
{
    const ItemPrototype* proto = item->GetProto();
    if (player->GetLevel() < vendorBelowLevel && proto->SellPrice && (proto->Quality <= ITEM_QUALITY_NORMAL || item->IsSoulBound()))
        return EItemEval::Vendor;

    if (proto->Bonding == BIND_WHEN_EQUIPPED && !item->IsSoulBound())
        return EItemEval::AH;

    // TODO(keep): disenchanting, other specs, gear for a level reached soon, ...
    return EItemEval::Keep;
}
