#include "Task_UseObject.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/GameObject.h"
#include "Loot/LootMgr.h"
#include "Server/WorldSession.h"
#include "Spells/Spell.h"

using namespace ai;

namespace
{
    constexpr uint32 openTimeoutMs = 8000;
    constexpr uint32 useGraceMs = 1000;     // after the cast (or use), the loot window opens

    GameObject* GetObject(const Task_UseObjectContext& context)
    {
        Player* bot = context.bot;
        return !context.target.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetGameObject(context.target) : nullptr;
    }

    // takes the money and every item of the opened loot, then closes it
    void TakeLoot(Player* bot, const ObjectGuid& guid)
    {
        WorldSession* session = bot->GetSession();
        if (Loot* loot = sLootMgr.GetLoot(bot, guid))
        {
            if (loot->GetGoldAmount())
            {
                WorldPacket money(CMSG_LOOT_MONEY, 0);
                session->HandleLootMoneyOpcode(money);
            }
            LootItemList items;
            loot->GetLootItemsListFor(bot, items);
            for (const LootItem* item : items)
            {
                WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM, 1);
                take << uint8(item->lootSlot);
                session->HandleAutostoreLootItemOpcode(take);
            }
        }
        WorldPacket release(CMSG_LOOT_RELEASE, 8);
        release << guid;
        session->HandleLootReleaseOpcode(release);
    }
}

EActionRunStatus Task_UseObject::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_UseObject::OnEnterState);
    Task_UseObjectContext& context = ContextAs<Task_UseObjectContext>(baseContext);
    Player* bot = context.bot;
    GameObject* go = GetObject(context);
    context.waitedMs = 0;
    context.cast = false;
    context.result.clear();
    if (!go || !go->IsSpawned() || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    context.result = go->GetGOInfo()->name;
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);

    // a lock is opened by a spell (Opening), as the client casts it on a click
    if (const SpellEntry* spell = go->GetSpellForLock(bot))
    {
        SpellCastTargets targets;
        targets.setGOTarget(go);
        const SpellCastResult cast = bot->CastSpell(targets, spell, TRIGGERED_NONE);
        if (cast != SPELL_CAST_OK)
        {
            context.result += ": cast failed " + std::to_string(uint32(cast));
            return LogStatus(context, EActionRunStatus::Failed);
        }
        context.cast = true;
        return LogStatus(context, EActionRunStatus::Running);
    }

    WorldPacket use(CMSG_GAMEOBJ_USE, 8);
    use << go->GetObjectGuid();
    bot->GetSession()->HandleGameObjectUseOpcode(use);
    return LogStatus(context, EActionRunStatus::Running);
}

EActionRunStatus Task_UseObject::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_UseObject::OnTick);
    Task_UseObjectContext& context = ContextAs<Task_UseObjectContext>(baseContext);
    Player* bot = context.bot;
    if (bot->GetLootGuid() == context.target)
    {
        TakeLoot(bot, context.target);
        context.result += ": looted";
        return LogStatus(context, EActionRunStatus::Succeeded);
    }

    context.waitedMs += elapsed;
    if (bot->IsNonMeleeSpellCasted(false) && context.waitedMs < openTimeoutMs)
        return EActionRunStatus::Running;
    if (context.waitedMs < useGraceMs)
        return EActionRunStatus::Running;

    // a goober gives its credit on use, without a loot window
    GameObject* go = GetObject(context);
    if (go && go->GetGoType() == GAMEOBJECT_TYPE_GOOBER)
    {
        context.result += ": used";
        return LogStatus(context, EActionRunStatus::Succeeded);
    }

    context.result += context.cast ? ": no loot after the cast" : ": no loot";
    return LogStatus(context, EActionRunStatus::Failed);
}

std::string Task_UseObject::GetActivityInfo(const STAI_TaskContext& baseContext) const
{
    const Task_UseObjectContext& context = static_cast<const Task_UseObjectContext&>(baseContext);
    return context.result.empty() ? GetDebugInfo() : GetDebugInfo() + ": " + context.result;
}

const std::vector<STAI_PropertyDef>& Task_UseObject::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_UseObjectContext::target) };
    return properties;
}
