#pragma once
#include <unordered_map>
#include <vector>

#include <log4cxx/logger.h>
#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/helpers/exception.h>

#include "Entities/ObjectGuid.h"
#include "Common.h"

class Player;
class Map;

namespace instancebot
{
   struct PlayerData;

   class InstanceBot
   {
   public:
      InstanceBot(Map* map);

      void Init();

      void Update(const uint32& t_diff);

      void CheckBuffs();

      void OnPlayerEnter(Player* player);
      void OnPlayerLeave(Player* player);

      std::list<std::string> HandlePlayerbotCommand(char const* args, Player* master = NULL);

   private:
      log4cxx::LoggerPtr m_logger;

      Map* mp_map;
      std::unordered_map<ObjectGuid, PlayerData*> m_map;

      uint32 m_progress = 0;
      uint32 m_cooldown = 0;

      std::string m_dungeon_strat;
      
      bool AddAura(Unit* unit, uint32 spellId);
      std::vector<uint32> NeedGuildBuffs(Player* player);
   };
}