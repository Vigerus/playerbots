#include "PlayerbotUse.h"

#include "playerbot/ChatHelper.h"
#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/GameObject.h"
#include "Entities/Item.h"
#include "Entities/Player.h"
#include "Globals/ObjectMgr.h"
#include "Grids/CellImpl.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Loot/LootMgr.h"
#include "Spells/Spell.h"
#include "Spells/SpellMgr.h"
#include "World/World.h"

#include <algorithm>
#include <cctype>
#include <sstream>

using namespace ai;

namespace
{
    // game objects found by name / "go" this close
    constexpr float searchDistance = 30.0f;
    // the loot window of an open shows up within this long
    constexpr uint32 lootTimeoutMs = 3000;

    // the index of the spell the item casts when used, -1 when it has none
    int GetUseSpellIndex(const ItemPrototype* proto)
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            const _Spell& spell = proto->Spells[i];
            if (spell.SpellId > 0 && (spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_USE || spell.SpellTrigger == ITEM_SPELLTRIGGER_ON_NO_DELAY_USE))
                return i;
        }
        return -1;
    }

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return text;
    }

    // the nearest spawned game object, of the name when one is given
    GameObject* FindGameObject(Player* bot, const std::string& name)
    {
        GameObjectList gameObjects;
        MaNGOS::GameObjectInPosRangeCheck check(*bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), searchDistance);
        MaNGOS::GameObjectListSearcher<MaNGOS::GameObjectInPosRangeCheck> searcher(gameObjects, check);
        Cell::VisitGridObjects(bot, searcher, searchDistance);

        const std::string wanted = Lower(name);
        GameObject* nearest = nullptr;
        float nearestDistance = searchDistance;
        for (GameObject* go : gameObjects)
        {
            if (!sServerFacade.isSpawned(go))
                continue;
            if (!wanted.empty() && Lower(go->GetGOInfo()->name).find(wanted) == std::string::npos)
                continue;
            const float distance = bot->GetDistance(go);
            if (distance < nearestDistance)
            {
                nearest = go;
                nearestDistance = distance;
            }
        }
        return nearest;
    }
}

void PlayerbotUse::QueueCommand(const std::string& text, Player* requester)
{
    Request request;
    request.text = text;
    request.requester = requester->GetObjectGuid().GetRawValue();
    std::lock_guard<std::mutex> lock(queueLock);
    queue.push_back(request);
}

void PlayerbotUse::Update(PlayerbotAI* ai)
{
    std::vector<Request> requests;
    {
        std::lock_guard<std::mutex> lock(queueLock);
        requests.swap(queue);
    }

    for (const Request& request : requests)
    {
        Player* requester = sObjectMgr.GetPlayer(ObjectGuid(request.requester));
        if (!requester || !requester->IsInWorld())
            continue;

        HandleCommand(ai, requester, request.text);
    }

    if (looting)
        TakeLoot(ai);
}

void PlayerbotUse::HandleCommand(PlayerbotAI* ai, Player* requester, const std::string& text)
{
    Player* bot = ai->GetBot();
    if (text.empty())
    {
        ai->TellError(requester, "Usage: use <item> [<item>|<game object>], use <game object> or use go");
        return;
    }

    if (!sServerFacade.IsAlive(bot))
    {
        ai->TellError(requester, "I can't use anything while dead");
        return;
    }

    // the game object: a link, "go" for the nearest one
    GameObject* goTarget = nullptr;
    const std::list<ObjectGuid> goLinks = ChatHelper::parseGameobjects(text);
    if (!goLinks.empty())
    {
        goTarget = bot->GetMap()->GetGameObject(goLinks.front());
        if (!goTarget)
        {
            ai->TellError(requester, "I don't see that game object");
            return;
        }
    }
    else if (text == "go")
    {
        goTarget = FindGameObject(bot, "");
        if (!goTarget)
        {
            ai->TellError(requester, "There is no game object near me");
            return;
        }
    }

    // the items: links / ids in their order, else one of the bot's items by name
    std::vector<uint32> items = text == "go" ? std::vector<uint32>() : ChatHelper::parseItemsUnordered(text);
    if (items.empty() && text != "go" && goLinks.empty())
    {
        const std::list<Item*> found = ai->InventoryParseItems(text, IterateItemsMask(uint8(IterateItemsMask::ITERATE_ITEMS_IN_BAGS) | uint8(IterateItemsMask::ITERATE_ITEMS_IN_EQUIP)));
        if (!found.empty())
            items.push_back(found.front()->GetEntry());
    }

    if (items.empty())
    {
        // a game object near the bot by name
        if (!goTarget)
            goTarget = FindGameObject(bot, text);
        if (!goTarget)
        {
            ai->TellError(requester, "I have no " + text + " and there is none near me");
            return;
        }

        UseGameObject(ai, requester, goTarget);
        return;
    }

    Item* itemTarget = items.size() > 1 ? bot->GetItemByEntry(items[1]) : nullptr;
    if (items.size() > 1 && !itemTarget)
    {
        const ItemPrototype* proto = sObjectMgr.GetItemPrototype(items[1]);
        ai->TellError(requester, "I don't have " + (proto ? ChatHelper::formatItem(proto) : std::to_string(items[1])));
        return;
    }

    UseItem(ai, requester, items[0], itemTarget, goTarget);
}

bool PlayerbotUse::UseItem(PlayerbotAI* ai, Player* requester, uint32 itemId, Item* itemTarget, GameObject* goTarget)
{
    Player* bot = ai->GetBot();
    const ItemPrototype* proto = sObjectMgr.GetItemPrototype(itemId);
    if (!proto)
    {
        ai->TellError(requester, "Unknown item " + std::to_string(itemId));
        return false;
    }

    const std::string itemText = ChatHelper::formatItem(proto);
    const int spellIndex = GetUseSpellIndex(proto);
    const SpellEntry* spellInfo = spellIndex >= 0 ? sSpellTemplate.LookupEntry<SpellEntry>(proto->Spells[spellIndex].SpellId) : nullptr;

    // the bot's item, an equipped one first (trinkets, engineering helmets)
    Item* item = nullptr;
    for (Item* candidate : ai->InventoryParseItems(ChatHelper::formatQItem(itemId), IterateItemsMask(uint8(IterateItemsMask::ITERATE_ITEMS_IN_BAGS) | uint8(IterateItemsMask::ITERATE_ITEMS_IN_EQUIP))))
    {
        if (!item || candidate->IsEquipped())
            item = candidate;
    }

    if (!item)
    {
        // the item cheat: the spell without the item, not for items that are more than their spell
        const bool needsItem = proto->InventoryType != INVTYPE_NON_EQUIP || proto->StartQuest || proto->Class == ITEM_CLASS_QUEST ||
                               (proto->Flags & ITEM_FLAG_HAS_LOOT) || itemId == 6948 /* Hearthstone */;
        if (!ai->HasCheat(BotCheatMask::item) || needsItem || !spellInfo)
        {
            ai->TellError(requester, "I don't have " + itemText);
            return false;
        }

        if (!sServerFacade.IsSpellReady(bot, spellInfo->Id, itemId))
        {
            ai->TellError(requester, itemText + " is not ready yet");
            return false;
        }

        Unit* unitTarget = ai->GetUnit(requester->GetSelectionGuid());
        const bool cast = goTarget ? ai->CastSpell(spellInfo->Id, goTarget, itemTarget) : ai->CastSpell(spellInfo->Id, unitTarget ? unitTarget : bot, itemTarget);
        if (!cast)
        {
            ai->TellError(requester, "I can't use " + itemText);
            return false;
        }

        bot->AddCooldown(*spellInfo, proto, false);
        ai->TellPlayer(requester, "Using " + itemText);
        return true;
    }

    // items that start a quest
    if (proto->StartQuest)
    {
        const Quest* quest = sObjectMgr.GetQuestTemplate(proto->StartQuest);
        if (!quest)
            return false;

        WorldPacket packet(CMSG_QUESTGIVER_ACCEPT_QUEST, 8 + 4 + 4);
        packet << item->GetObjectGuid();
        packet << quest->GetQuestId();
        packet << uint32(0);
        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(packet);
        ai->TellPlayer(requester, "Accepted " + ChatHelper::formatQuest(quest));
        return true;
    }

    // items with loot and no spell (clams, boxes): opened, the loot goes to the bags
    if (!spellInfo && (proto->Flags & ITEM_FLAG_HAS_LOOT))
    {
        if (proto->LockID && !item->HasFlag(ITEM_FIELD_FLAGS, ITEM_DYNFLAG_UNLOCKED))
        {
            ai->TellError(requester, itemText + " is locked");
            return false;
        }

        std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_OPEN_ITEM, 2));
        *packet << item->GetBagSlot();
        *packet << item->GetSlot();
        bot->GetSession()->QueuePacket(std::move(packet));
        looting = true;
        lootStartMs = WorldTimer::getMSTime();
        lootRequester = requester->GetObjectGuid().GetRawValue();
        ai->TellPlayer(requester, "Opening " + itemText);
        return true;
    }

    if (!spellInfo)
    {
        ai->TellError(requester, itemText + " can't be used");
        return false;
    }

    // the core's checks again, to answer why
    if (proto->InventoryType != INVTYPE_NON_EQUIP && !item->IsEquipped())
    {
        ai->TellError(requester, itemText + " has to be equipped");
        return false;
    }
    if (bot->CanUseItem(item) != EQUIP_ERR_OK || item->IsInTrade())
    {
        ai->TellError(requester, "I can't use " + itemText);
        return false;
    }
    if (bot->IsInCombat() && IsNonCombatSpell(spellInfo))
    {
        ai->TellError(requester, "I can't use " + itemText + " in combat");
        return false;
    }
    if (!sServerFacade.IsSpellReady(bot, spellInfo->Id, itemId))
    {
        ai->TellError(requester, itemText + " is not ready yet");
        return false;
    }

    // the target: the given item / game object, the requester's selection, else the bot
    Unit* unitTarget = goTarget || itemTarget ? nullptr : ai->GetUnit(requester->GetSelectionGuid());
    const bool location = spellInfo->Targets & TARGET_FLAG_DEST_LOCATION ||
                          spellInfo->EffectImplicitTargetA[0] == TARGET_ENUM_UNITS_ENEMY_AOE_AT_DEST_LOC ||
                          (proto->Class == ITEM_CLASS_TRADE_GOODS && proto->SubClass == ITEM_SUBCLASS_EXPLOSIVES);
    const bool wantsItem = (spellInfo->Targets & TARGET_FLAG_ITEM) != 0;
    if (!itemTarget && wantsItem)
        itemTarget = ai->GetItemForSpell(spellInfo->Id);

    SpellCastTargets targets;
    std::string targetText;
    if (itemTarget)
    {
        targets.setItemTarget(itemTarget);
        targetText = ChatHelper::formatItem(itemTarget);
    }
    else if (goTarget)
    {
        if (location)
            targets.setDestination(goTarget->GetPositionX(), goTarget->GetPositionY(), goTarget->GetPositionZ());
        else
            targets.setGOTarget(goTarget);
        targetText = ChatHelper::formatGameobject(goTarget);
    }
    else if (unitTarget && unitTarget != bot)
    {
        if (location)
            targets.setDestination(unitTarget->GetPositionX(), unitTarget->GetPositionY(), unitTarget->GetPositionZ());
        else
            targets.setUnitTarget(unitTarget);
        targetText = unitTarget->GetName();
    }
    else if (wantsItem)
    {
        ai->TellError(requester, "I have nothing to use " + itemText + " on");
        return false;
    }
    else
    {
        targets.setUnitTarget(bot);
    }

    // a cast item stops the bot like a cast spell
    if (GetSpellCastTime(spellInfo, bot) > 0 || IsChanneledSpell(spellInfo))
        ai->StopMoving();

    // CMSG_USE_ITEM: <bag><slot><spell index><targets>, handled with the bot's session like a client's
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_USE_ITEM, 20));
    *packet << item->GetBagSlot();
    *packet << item->GetSlot();
    *packet << uint8(spellIndex);
    targets.write(*packet);
    bot->GetSession()->QueuePacket(std::move(packet));

    std::ostringstream out;
    out << "Using " << itemText;
    if (!targetText.empty())
        out << " on " << targetText;
    if (proto->Stackable > 1 && item->GetCount() > 1)
        out << " (" << item->GetCount() - 1 << " left)";
    else if (proto->Stackable > 1)
        out << " (the last one)";
    ai->TellPlayer(requester, out.str());
    return true;
}

bool PlayerbotUse::UseGameObject(PlayerbotAI* ai, Player* requester, GameObject* go)
{
    Player* bot = ai->GetBot();
    const std::string goText = ChatHelper::formatGameobject(go);
    if (!sServerFacade.isSpawned(go) || go->IsInUse() || go->GetGoState() != GO_STATE_READY)
    {
        ai->TellError(requester, "I can't use " + goText);
        return false;
    }

    if (!go->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
    {
        ai->TellError(requester, "I'm too far away from " + goText);
        return false;
    }

    // CMSG_GAMEOBJ_USE: <guid>; chests open a loot window, taken in Update
    std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_GAMEOBJ_USE, 8));
    *packet << go->GetObjectGuid();
    bot->GetSession()->QueuePacket(std::move(packet));

    if (go->GetGoType() == GAMEOBJECT_TYPE_CHEST)
    {
        looting = true;
        lootStartMs = WorldTimer::getMSTime();
        lootRequester = requester->GetObjectGuid().GetRawValue();
        ai->TellPlayer(requester, "Looting " + goText);
    }
    else
        ai->TellPlayer(requester, "Using " + goText);
    return true;
}

void PlayerbotUse::TakeLoot(PlayerbotAI* ai)
{
    Player* bot = ai->GetBot();
    const ObjectGuid lootGuid = bot->GetLootGuid();
    Loot* loot = lootGuid ? sLootMgr.GetLoot(bot, lootGuid) : nullptr;
    if (!loot)
    {
        // not open yet (the packet waits for the session), or the open failed
        if (WorldTimer::getMSTimeDiff(lootStartMs, WorldTimer::getMSTime()) >= lootTimeoutMs)
            looting = false;
        return;
    }

    looting = false;
    const bool stored = loot->AutoStore(bot);
    // the gold also releases the loot when everything is taken (and may free it)
    loot->SendGold(bot);
    if (Loot* left = bot->GetLootGuid() ? sLootMgr.GetLoot(bot, bot->GetLootGuid()) : nullptr)
        left->Release(bot);

    if (!stored)
    {
        Player* requester = sObjectMgr.GetPlayer(ObjectGuid(lootRequester));
        if (requester && requester->IsInWorld())
            ai->TellError(requester, "My bags are full");
    }
}
