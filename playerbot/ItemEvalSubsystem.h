#pragma once

#include "World/WorldSubsystem.h"

class Item;
class Player;

// what a player should do with an item it carries
enum class EItemEval : uint8
{
    Vendor,     // worse, and worth only its vendor price
    AH,         // worse, but tradeable (bind on equip, not bound yet)
    Keep,       // not judged (not gear, a special bag, or worse but neither Vendor nor AH)
    Equip       // better than what is worn (an unused slot counts as worse than anything)
};

const char* ToString(EItemEval eval);

/*
 * Judges an item of a player against what it wears at the matching slot, or against its bags if it is a bag:
 *
 *   if (ItemEvalSubsystem* itemEval = sWorld.GetSubsystem<ItemEvalSubsystem>())
 *       if (itemEval->Evaluate(bot, item) == EItemEval::Vendor)
 *           ...
 *
 * Gear is compared by the class / spec stat weights of sRandomItemMgr, then quality, then item level. A worse item is
 * Vendor below level 60 when it is poor, common or soulbound; else AH when it binds on equip and is not bound yet; else
 * Keep. Holds no state: any thread that owns the player (its map thread).
 */
class ItemEvalSubsystem : public WorldSubsystem
{
    public:
        EItemEval Evaluate(Player* player, Item* item) const;
        // the slot (bag 0) an Equip item goes to: a free one, else the weakest worn item's; NULL_SLOT for other items
        uint8 FindEquipSlot(Player* player, Item* item) const;

    private:
        uint8 FindGearSlot(Player* player, Item* item) const;
        uint8 FindBagSlot(Player* player, Item* item) const;
        static EItemEval EvaluateWorse(Player* player, Item* item);
};
