#include "InstanceBot.h"
#include "Entities/Player.h"
#include "Log/LogHelper.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/ServerFacade.h"

#include <sstream>
#include <boost/algorithm/string.hpp>

#include "GuildProgressSubsystem.h"

#define ALL_CLASSES 0

namespace instancebot {

   struct PlayerData
   {
      std::vector<std::string> bip;
   };

   InstanceBot::InstanceBot(Map* map) :
      m_logger(log4cxx::Logger::getLogger("InstanceBot")),
      mp_map(map)
   {
      CCLOG_TRACE(m_logger) << " Source:[InstanceBot] ctor";

      m_dungeon_strat = "brd_arena_run";
   }

   void InstanceBot::Init()
   {
      
   }

   bool InstanceBot::AddAura(Unit* unit, uint32 spellId)
   {
      // number or [name] Shift-click form |color|Hspell:spell_id|h[name]|h|r or Htalent form    

      SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellId);
      if (!spellInfo)
         return false;

      if (!IsSpellAppliesAura(spellInfo, (1 << EFFECT_INDEX_0) | (1 << EFFECT_INDEX_1) | (1 << EFFECT_INDEX_2)) &&
         !IsSpellHaveEffect(spellInfo, SPELL_EFFECT_PERSISTENT_AREA_AURA))
      {
         return false;
      }

      SpellAuraHolder* holder = CreateSpellAuraHolder(spellInfo, unit, unit);

      for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
      {
         uint8 eff = spellInfo->Effect[i];
         if (eff >= MAX_SPELL_EFFECTS)
            continue;
         if (IsAreaAuraEffect(eff) ||
            eff == SPELL_EFFECT_APPLY_AURA ||
            eff == SPELL_EFFECT_PERSISTENT_AREA_AURA)
         {
            int32 basePoints = spellInfo->CalculateSimpleValue(SpellEffectIndex(i));
            int32 damage = basePoints;
            Aura* aur = CreateAura(spellInfo, SpellEffectIndex(i), &damage, &basePoints, holder, unit);
            holder->AddAura(aur, SpellEffectIndex(i));
         }
      }
      if (!unit->AddSpellAuraHolder(holder))
         delete holder;

      return true;
   }

   std::vector<uint32> InstanceBot::NeedGuildBuffs(Player* player)
   {
      if (player == nullptr)
      {
          return { };
      }

      std::vector<uint32> retVec;
      uint8 classId = player->getClass();

      // buffs collection
      for (uint32 spellId : GuildProgressSubsystem::instance().GetGuildBuffsForClass(player->GetGuildId(), classId))
      {
          if (player->HasAura(spellId))
              continue;

          retVec.push_back(spellId);
      }

      // item generation
      for (uint32 itemId : GuildProgressSubsystem::instance().GetGuildItemsForClass(player->GetGuildId(), classId))
      {
          if (player->GetItemByEntry(itemId) == nullptr)
          {
              player->StoreNewItemInBestSlots(itemId, 1);
          }
      }

      return retVec;
   }

   void InstanceBot::OnPlayerEnter(Player* player)
   {
      m_map.insert(std::make_pair(player->GetObjectGuid(), new PlayerData));

      std::ostringstream welcome_message;
      welcome_message << "Welcome to InstanceBot for " <<  mp_map->GetMapName();
      welcome_message << " strat: " << m_dungeon_strat;
      welcome_message << " progress: " << m_progress;

      WorldPacket data;
      ChatHandler::BuildChatPacket(data, CHAT_MSG_BG_SYSTEM_NEUTRAL, welcome_message.str().c_str(), LANG_UNIVERSAL);
      sServerFacade.SendPacket(player, data);


      std::vector<uint32> pbuff = NeedGuildBuffs(player);
      for (uint32 aura : pbuff)
         AddAura(player, aura);

//       if (PlayerbotAI* bot = player->GetPlayerbotAI())
//       {
//          if (Player* master = bot->GetMaster())
//          {
//             std::vector<std::string> lines;
// 
//             if (bot->IsTank(player))
//                lines.push_back("co +rtsc");
//             else
//                lines.push_back("co +threat,+rtsc");
// 
//             lines.push_back("ll -vendor,-consumable");
// 
//             for (auto& l : lines)
//             {
//                ChatHandler::BuildChatPacket(data, CHAT_MSG_WHISPER, l.c_str(), LANG_UNIVERSAL, CHAT_TAG_NONE, master->GetObjectGuid(), master->GetName());
//                //sServerFacade.SendPacket(player, data);
//                bot->HandleMasterIncomingPacket(data);
//             }
//             
//          }
//          
//       }


   }

   void InstanceBot::OnPlayerLeave(Player* player)
   {
      auto it = m_map.find(player->GetObjectGuid());

      if (it == m_map.end())
         return;

      delete it->second;
      m_map.erase(it);
   }

   void InstanceBot::Update(const uint32& t_diff)
   {
      if (m_cooldown < t_diff)
      {
         m_cooldown = 10000;

         CheckBuffs();
      }
      else
      {
         m_cooldown -= t_diff;
      }
   }

   void InstanceBot::CheckBuffs()
   {
      for (auto& el : m_map)
      {
         Player* player = mp_map->GetPlayer(el.first);

         std::vector<uint32> pbuff = NeedGuildBuffs(player);
         for (uint32 aura : pbuff)
            AddAura(player, aura);
      }
   }

   std::list<std::string> InstanceBot::HandlePlayerbotCommand(char const* args, Player* master)
   {
      std::list<std::string> l;


      return l;
   }
}