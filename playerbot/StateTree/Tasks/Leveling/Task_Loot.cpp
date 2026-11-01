#include "Task_Loot.h"

#include "playerbot/PlayerbotAI.h"
#include "playerbot/ServerFacade.h"

#include "Entities/Creature.h"
#include "Loot/LootMgr.h"
#include "Server/WorldSession.h"

using namespace ai;

EActionRunStatus Task_Loot::OnEnterState(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(Task_Loot::OnEnterState);
    Task_LootContext& context = ContextAs<Task_LootContext>(baseContext);
    Player* bot = context.bot;
    Creature* corpse = !context.target.IsEmpty() && bot->IsInWorld() ? bot->GetMap()->GetCreature(context.target) : nullptr;
    if (!corpse || corpse->IsAlive() || !corpse->m_loot || !corpse->m_loot->CanLoot(bot) || !sServerFacade.IsAlive(bot))
        return EActionRunStatus::Failed;

    StartLog(context);
    WorldSession* session = bot->GetSession();
    const ObjectGuid guid = corpse->GetObjectGuid();

    // the client stands up and opens the corpse; the core checks the distance and the right to it
    if (!bot->IsStandState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    WorldPacket open(CMSG_LOOT, 8);
    open << guid;
    session->HandleLootOpcode(open);
    if (bot->GetLootGuid() != guid)
        return LogStatus(context, EActionRunStatus::Failed);

    Loot* loot = corpse->m_loot;
    if (loot->GetGoldAmount())
    {
        WorldPacket money(CMSG_LOOT_MONEY, 0);
        session->HandleLootMoneyOpcode(money);
    }

    // each item the bot may take; one that does not fit in the bags stays in the corpse
    LootItemList items;
    loot->GetLootItemsListFor(bot, items);
    for (const LootItem* item : items)
    {
        WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM, 1);
        take << uint8(item->lootSlot);
        session->HandleAutostoreLootItemOpcode(take);
    }

    // the corpse may be gone with its loot once everything is taken
    WorldPacket release(CMSG_LOOT_RELEASE, 8);
    release << guid;
    session->HandleLootReleaseOpcode(release);
    return LogStatus(context, EActionRunStatus::Succeeded);
}

const std::vector<STAI_PropertyDef>& Task_Loot::GetProperties() const
{
    static const std::vector<STAI_PropertyDef> properties = { STAI_PropertyDef::Of("target", &Task_LootContext::target) };
    return properties;
}
