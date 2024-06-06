
#include "playerbot/playerbot.h"
#include "LootRollAction.h"
#include "playerbot/strategy/values/ItemUsageValue.h"
#include "playerbot/strategy/values/LootValues.h"

using namespace ai;

bool LootStartRollAction::Execute(Event& event)
{
    WorldPacket p(event.getPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid creatureGuid;
    uint32 itemSlot;
    uint32 itemId;
    uint32 randomSuffix;
    int32 randomPropertyId;
#ifdef MANGOSBOT_TWO
    uint32 mapId;
    uint32 count;
#endif 
    uint32 timeout;

    p.rpos(0); //reset packet pointer
    p >> creatureGuid; //creature guid what we're looting
#ifdef MANGOSBOT_TWO
    p >> mapId; /// 3.3.3 mapid
#endif 
    p >> itemSlot; // the itemEntryId for the item that shall be rolled for
    p >> itemId; // the itemEntryId for the item that shall be rolled for
    p >> randomSuffix; // randomSuffix
    p >> randomPropertyId; // item random property ID
#ifdef MANGOSBOT_TWO
    p >> count; // items in stack
#endif 
    p >> timeout;  // the countdown time to choose "need" or "greed"

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    if (lootRolls.find(creatureGuid) != lootRolls.end())
        return false;

    Loot* loot = sLootMgr.GetLoot(bot, creatureGuid);
    if (!loot)
        return false;

    for(uint8 i=0;i< MAX_NR_LOOT_ITEMS;i++)
        if(loot->GetRollForSlot(i))
            lootRolls.insert({ creatureGuid, i });
        
    ActiveRolls::CleanUp(bot,lootRolls);

    SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);

    return false;
}

bool RollAction::Execute(Event& event)
{      
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();

    if (text.empty())
    {
        ai->TellPlayerNoFacing(requester, "Please give a roll type or item. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    ItemIds ids = ChatHelper::parseItems(text);

    std::string type = "auto";
    if (ids.empty())
        type = text;
    else
        type = text.substr(0, text.find(" "));

    if (type == "emote")
    {
        std::vector<std::string> args = ChatHelper::splitString(text, " ");

        if (args.size() == 2)
            args = { args[0], "1", args[1] };
        if (args.size() == 1)
            args = { args[0], "1", "100" };

        for (char& d : args[1]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        for (char& d : args[2]) //Check if itemId contains only numbers
            if (!isdigit(d))
                return false;

        WorldPacket data(MSG_RANDOM_ROLL);
        data << stoi(args[1]);
        data << stoi(args[2]);
        bot->GetSession()->HandleRandomRollOpcode(data);

        return true;
    }

    if (!bot->GetGroup())
        return false;

    if (AI_VALUE(LootRollMap, "active rolls").empty())
        return false;

    if (AI_VALUE(uint8, "bag space") >= 100)
        return false;

    if (type != "need" && type != "greed" && type != "pass" && type != "auto")
    {
        ai->TellPlayerNoFacing(requester, "Please give a correct roll type. need, greed, pass or auto. See " + ChatHelper::formatValue("help", "action:roll", "roll help") + " for more information.");
        return false;
    }

    RollVote vote = ROLL_NOT_VALID;

    if (type.find("need") == 0)
        vote = ROLL_NEED;
    else if (type.find("greed") == 0)
        vote = ROLL_GREED;
    else if (type.find("pass") == 0)
        vote = ROLL_PASS;

    uint32 rolledItems = 0;

    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    for (auto roll : lootRolls)
    {
        ItemQualifier itemQualifier = GetRollItem(roll.first, roll.second);

        if (!itemQualifier.GetId())
            continue;

        if (!ids.empty() && ids.find(itemQualifier.GetId()) == ids.end())
            continue;

        RollVote doVote = vote;
        if (doVote == ROLL_NOT_VALID) //Auto
            doVote = CalculateRollVote(itemQualifier);

        rolledItems += RollOnItemInSlot(doVote, roll.first, roll.second);     
    }

    return rolledItems;
}

ItemQualifier RollAction::GetRollItem(ObjectGuid lootGuid, uint32 slot)
{
    Loot* loot = sLootMgr.GetLoot(bot, lootGuid);
    if (!loot)
        return ItemQualifier();

    LootItem* item = loot->GetLootItemInSlot(slot);

    if (!item)
        return ItemQualifier();

    return ItemQualifier(item);
}

RollVote RollAction::CalculateRollVote(ItemQualifier& itemQualifier)
{
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemQualifier.GetQualifier());

    RollVote needVote = ROLL_PASS;
    switch (usage)
    {
    case ItemUsage::ITEM_USAGE_EQUIP:
    case ItemUsage::ITEM_USAGE_GUILD_TASK:
    case ItemUsage::ITEM_USAGE_FORCE_NEED:
    case ItemUsage::ITEM_USAGE_BAD_EQUIP:
        needVote = ROLL_NEED;
        break;
    case ItemUsage::ITEM_USAGE_SKILL:
    case ItemUsage::ITEM_USAGE_USE:
    case ItemUsage::ITEM_USAGE_DISENCHANT:
    case ItemUsage::ITEM_USAGE_AH:
    case ItemUsage::ITEM_USAGE_VENDOR:
    case ItemUsage::ITEM_USAGE_FORCE_GREED:
        needVote = ROLL_GREED;
        break;
    }
    return StoreLootAction::IsLootAllowed(itemQualifier, bot->GetPlayerbotAI()) ? needVote : ROLL_PASS;
}

bool RollAction::RollOnItemInSlot(RollVote vote, ObjectGuid lootGuid, uint32 slot)
{
    Loot* loot = sLootMgr.GetLoot(bot, lootGuid);
    if (!loot)
        return false;

    LootItem* item = loot->GetLootItemInSlot(slot);
    ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(item->itemId);
    if (!proto)
        return false;

    GroupLootRoll* lootRoll = loot->GetRollForSlot(slot);
    if (!lootRoll)
        return false;

    bool didRoll = lootRoll->PlayerVote(bot, vote);

    if (didRoll)
    {
        LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

        ActiveRolls::CleanUp(bot, lootRolls, lootGuid, slot);

        SET_AI_VALUE(LootRollMap, "active rolls", lootRolls);
    }

    return didRoll;
}

bool LootRollAction::Execute(Event& event)
{
    Player* bot = QueryItemUsageAction::ai->GetBot();

    WorldPacket p(event.getPacket()); //WorldPacket packet for CMSG_LOOT_ROLL, (8+4+1)
    ObjectGuid guid;
    uint32 slot;
    uint8 rollType;
    p.rpos(0); //reset packet pointer
    p >> guid; //guid of the item rolled
    p >> slot; //number of players invited to roll
    p >> rollType; //need,greed or pass on roll

    ItemQualifier itemQualifier = GetRollItem(guid, slot);

    if (!itemQualifier.GetId())
        return false;

    RollVote vote = CalculateRollVote(itemQualifier);

    return RollOnItemInSlot(vote, guid, slot);
}

bool AutoLootRollAction::Execute(Event& event)
{
    LootRollMap lootRolls = AI_VALUE(LootRollMap, "active rolls");

    auto currentRoll = lootRolls.begin();

    currentRoll = std::next(currentRoll, urand(0, lootRolls.size() - 1));

    ItemQualifier itemQualifier = GetRollItem(currentRoll->first, currentRoll->second);

    if (!itemQualifier.GetId())
        return false;

    RollVote vote = CalculateRollVote(itemQualifier);

    return RollOnItemInSlot(vote, currentRoll->first, currentRoll->second);
}

bool AutoLootRollAction::isPossible()
{
    return bot->GetGroup() && !AI_VALUE(LootRollMap, "active rolls").empty() && AI_VALUE(uint8, "bag space") < 100;
}

std::vector<uint32> LootCouncilAction::StringToSlots(const std::string& str)
{
   uint32 slot = ChatHelper::parseSlot(str);

   if (slot != EQUIPMENT_SLOT_END)
   {
      return { slot };
   }

   std::vector<uint32> slots;

   if (str == "finger" || str == "ring")
   {
      slots.push_back(EQUIPMENT_SLOT_FINGER1);
      slots.push_back(EQUIPMENT_SLOT_FINGER2);
   }
   else if (str == "trinket")
   {
      slots.push_back(EQUIPMENT_SLOT_TRINKET1);
      slots.push_back(EQUIPMENT_SLOT_TRINKET2);
   }
   else if (str == "weapon")
   {
      slots.push_back(EQUIPMENT_SLOT_MAINHAND);
      slots.push_back(EQUIPMENT_SLOT_OFFHAND);
      slots.push_back(EQUIPMENT_SLOT_RANGED);
   }

   return slots;
}

std::vector<uint32> LootCouncilAction::InventoryTypeToSlots(InventoryType inv_type)
{
   std::vector<uint32> slots;

   switch (inv_type)
   {
   case INVTYPE_HEAD:
      slots.push_back(EQUIPMENT_SLOT_HEAD);
      break;
   case INVTYPE_NECK:
      slots.push_back(EQUIPMENT_SLOT_NECK);
      break;
   case INVTYPE_SHOULDERS:
      slots.push_back(EQUIPMENT_SLOT_SHOULDERS);
      break;
   case INVTYPE_CHEST:
      slots.push_back(EQUIPMENT_SLOT_CHEST);
      break;
   case INVTYPE_WAIST:
      slots.push_back(EQUIPMENT_SLOT_WAIST);
      break;
   case INVTYPE_LEGS:
      slots.push_back(EQUIPMENT_SLOT_LEGS);
      break;
   case INVTYPE_FEET:
      slots.push_back(EQUIPMENT_SLOT_FEET);
      break;
   case INVTYPE_WRISTS:
      slots.push_back(EQUIPMENT_SLOT_WRISTS);
      break;
   case INVTYPE_HANDS:
      slots.push_back(EQUIPMENT_SLOT_HANDS);
      break;
   case INVTYPE_FINGER:
      slots.push_back(EQUIPMENT_SLOT_FINGER1);
      slots.push_back(EQUIPMENT_SLOT_FINGER2);
      break;
   case INVTYPE_TRINKET:
      slots.push_back(EQUIPMENT_SLOT_TRINKET1);
      slots.push_back(EQUIPMENT_SLOT_TRINKET2);
      break;
   case INVTYPE_CLOAK:
      slots.push_back(EQUIPMENT_SLOT_BACK);
      break;
   case INVTYPE_RANGED:
   case INVTYPE_WEAPON:
   case INVTYPE_SHIELD:
   case INVTYPE_2HWEAPON:
   case INVTYPE_WEAPONMAINHAND:
   case INVTYPE_WEAPONOFFHAND:
   case INVTYPE_THROWN:
   case INVTYPE_RANGEDRIGHT:
      slots.push_back(EQUIPMENT_SLOT_MAINHAND);
      slots.push_back(EQUIPMENT_SLOT_OFFHAND);
      slots.push_back(EQUIPMENT_SLOT_RANGED);
      break;
   default:
      break;
   }

   return slots;
}

std::string LootCouncilAction::LinkItemsForSlots(const std::vector<uint32>& slots)
{
   std::ostringstream ss;

   for (uint32 slot : slots)
   {
      Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (!pItem)
      {
         continue;
      }

      std::string item = ChatHelper::formatItem(pItem);

      ss << " " << item;
   }

   return ss.str();
}

bool LootCouncilAction::Execute(Event& event)
{
   if (Player* requester = event.getOwner())
   {
      std::string text = event.getParam();

      std::ostringstream ss;

      if (ChatHelper::startswith(text, "link"))
      {
         std::vector<std::string> s = ChatHelper::splitString(text, " ");

         if (s.size() != 2)
         {
            return false;
         }

         std::vector<uint32> slots = StringToSlots(s[1]);

         ss << LinkItemsForSlots(slots);
      }
      else
      {
         std::set<std::string> qualifiers = ChatHelper::parseItemQualifiers(text);
         for (auto qualifier : qualifiers)
         {
            ItemQualifier itemQualifier(qualifier);
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", itemQualifier.GetQualifier());

            if (usage != ItemUsage::ITEM_USAGE_EQUIP &&
               usage != ItemUsage::ITEM_USAGE_KEEP)
            {
               continue;
            }

            ItemPrototype const* proto = itemQualifier.GetProto();
            
            if (proto == nullptr)
            {
               continue;
            }

            InventoryType inv_type = (InventoryType)proto->InventoryType;
            ss << "I need, i have: " << LinkItemsForSlots(InventoryTypeToSlots(inv_type));
         }
      }

      ai->TellPlayerNoFacing(requester, ss.str());
      return true;
   }

   return false;
}

bool LootCouncilAction::isPossible()
{
   return true;
}
