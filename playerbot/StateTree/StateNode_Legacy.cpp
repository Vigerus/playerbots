#include "StateNode_Legacy.h"

void StateNode_Legacy::Update(uint32 elapsed, bool minimal)
{
    bool doMinimalReaction = minimal;
    if (bot->IsTaxiFlying())
        doMinimalReaction = true;
    if (ai->UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()))
    {
        return;
    }
    if (!ai->CanUpdateAIInternal())
        return;
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && currentSpell->getState() == SPELL_STATE_CASTING && currentSpell->GetCastedTime() > 0U)
    {
        ai->SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());
        if (!ai->CanUpdateAIInternal())
            return;
    }
    ai->UpdateAIInternal(elapsed, minimal);
    bool min = minimal;
    if (!bot->IsInCombat())
        min = true;
    if (bot && ai->HasRealPlayerMaster())
        min = false;
    ai->YieldAIInternalThread(min);
}