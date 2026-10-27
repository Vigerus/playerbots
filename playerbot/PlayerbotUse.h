#pragma once

#include "Entities/ObjectGuid.h"
#include "Platform/Define.h"

#include <mutex>
#include <string>
#include <vector>

class GameObject;
class Item;
class Player;
class PlayerbotAI;
class Unit;

namespace ai
{
    // "use <what>" / "u <what>" chat commands (whispered, or in party chat to every bot):
    //   use <item>                  uses the item (link, id or name, bags and equipped) on the requester's selection,
    //                               or on the bot itself; a location spell (explosives) goes off at the selection
    //   use <item> <item>           uses the first item on the second one (sharpening stones, poisons, ...)
    //   use <item> <game object>    uses the item on the game object (link, or a name of one nearby)
    //   use <game object>           uses the game object (link, a name of one nearby, or "go" for the nearest one)
    // Items with loot (clams, boxes) are opened and chests are looted: what they hold goes to the bags. Items that
    // start a quest give the quest. With the item cheat the bot casts the item's spell without having the item
    // (not for quest items and items that have to be equipped).
    // The uses go through the client's packets (CMSG_USE_ITEM, CMSG_GAMEOBJ_USE, CMSG_OPEN_ITEM): the core checks them.
    // Queued from the chat and run in the bot's update (Update).
    class PlayerbotUse
    {
    public:
        // the text after "use " / "u ", from HandleCommand
        void QueueCommand(const std::string& text, Player* requester);
        // runs what was queued and loots what an open left (the bot's update, map thread)
        void Update(PlayerbotAI* ai);

    private:
        struct Request
        {
            std::string text;
            uint64 requester = 0;
        };

        void HandleCommand(PlayerbotAI* ai, Player* requester, const std::string& text);
        bool UseItem(PlayerbotAI* ai, Player* requester, uint32 itemId, Item* itemTarget, GameObject* goTarget);
        bool UseGameObject(PlayerbotAI* ai, Player* requester, GameObject* go);
        // the loot window an open / a chest left: everything to the bags
        void TakeLoot(PlayerbotAI* ai);

        std::mutex queueLock;
        std::vector<Request> queue;

        // an item / a chest was opened: its loot is taken once the window is there
        bool looting = false;
        uint32 lootStartMs = 0;
        uint64 lootRequester = 0;
    };
}
