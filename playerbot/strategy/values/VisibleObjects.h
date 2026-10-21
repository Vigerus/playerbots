#pragma once

#include "Entities/Player.h"
#include "Maps/Map.h"
#include "Grids/GridNotifiers.h"
#include "Grids/GridNotifiersImpl.h"
#include "Grids/CellImpl.h"

#include <list>

namespace ai
{
    // Objects around a player without a grid search: the core keeps what is visible to every player (bots included)
    // up to date on visibility changes (Player::AddAtClient / RemoveAtClient, the client's create / out of range
    // updates), so the candidates are read from that set (Player::GetClientGuids) and filtered with the same checks a
    // grid search would use. Only what the player can see is found (stealth, despawned objects, the visibility distance
    // of the map); a range that reaches past the map's visibility distance falls back to the grid search.
    class VisibleObjects
    {
    public:
        // playersOnly skips the creature guids before looking the units up
        template<class Check>
        static void FindUnits(Player* player, float range, Check& check, std::list<Unit*>& targets, bool playersOnly = false)
        {
            if (!Covers(player, range))
            {
                MaNGOS::UnitListSearcher<Check> searcher(targets, check);
                Cell::VisitAllObjects(player, searcher, range);
                return;
            }

            Map* map = player->GetMap();
            for (const ObjectGuid& guid : player->GetClientGuids())
            {
                if (playersOnly ? !guid.IsPlayer() : !guid.IsUnit())
                    continue;

                Unit* unit = map->GetUnit(guid);
                if (unit && unit != player && check(unit))
                    targets.push_back(unit);
            }
        }

        template<class Check>
        static void FindGameObjects(Player* player, float range, Check& check, std::list<GameObject*>& targets)
        {
            if (!Covers(player, range))
            {
                MaNGOS::GameObjectListSearcher<Check> searcher(targets, check);
                Cell::VisitAllObjects((const WorldObject*)player, searcher, range);
                return;
            }

            Map* map = player->GetMap();
            for (const ObjectGuid& guid : player->GetClientGuids())
            {
                // elevators and the like are game objects with a transport guid (MO transports are never in the set)
                if (!guid.IsGameObject() && !guid.IsTransport())
                    continue;

                GameObject* go = map->GetGameObject(guid);
                if (go && check(go))
                    targets.push_back(go);
            }
        }

#ifndef MANGOSBOT_TWO
        template<class Check>
        static void FindDynamicObjects(Player* player, float range, Check& check, std::list<DynamicObject*>& targets)
        {
            if (!Covers(player, range))
            {
                MaNGOS::DynamicObjectListSearcher<Check> searcher(targets, check);
                Cell::VisitAllObjects((const WorldObject*)player, searcher, range);
                return;
            }

            Map* map = player->GetMap();
            for (const ObjectGuid& guid : player->GetClientGuids())
            {
                if (!guid.IsDynamicObject())
                    continue;

                DynamicObject* dynObject = map->GetDynamicObject(guid);
                if (dynObject && check(dynObject))
                    targets.push_back(dynObject);
            }
        }
#endif

    private:
        // the visible set holds everything within range
        static bool Covers(Player* player, float range)
        {
            Map* map = player->GetMap();
            return map && player->IsInWorld() && range <= map->GetVisibilityDistance();
        }
    };
}
