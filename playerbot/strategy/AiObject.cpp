
#include "playerbot/playerbot.h"
#include "AiObject.h"
#include "Entities/Player.h"

AiObject::AiObject(PlayerbotAI* ai) :
    PlayerbotAIAware(ai),
    bot(ai->GetBot()),
    context(ai->GetAiObjectContext()),
    chat(ai->GetChatHelper())
{
}

Player* AiObject::GetMaster() const
{
    return ai ? ai->GetMaster() : nullptr;
}
