#include "StateNode_Legacy.h"

#include "playerbot/AiFactory.h"

static BotState GetBotState(HLSStateType stateType)
{
    switch (stateType)
    {
        case ai::HLSStateType::NonCombat: return BotState::BOT_STATE_NON_COMBAT;
        case ai::HLSStateType::Combat: return BotState::BOT_STATE_COMBAT;
        case ai::HLSStateType::Reaction: return BotState::BOT_STATE_REACTION;
        case ai::HLSStateType::Dead: return BotState::BOT_STATE_DEAD;
        default: break;
    }
    return BotState::BOT_STATE_ALL;
}

void StateNode_Legacy::Update(uint32 elapsed, bool minimal)
{
    if (!ai->CanUpdateAIInternal())
        return;
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && currentSpell->getState() == SPELL_STATE_CASTING && currentSpell->GetCastedTime() > 0U)
    {
        ai->SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());
        if (!ai->CanUpdateAIInternal())
            return;
    }
    if (ai->UpdateAIInternal(elapsed, minimal))
    {
        ai->DoNextAction(minimal, &ai->GetCurrentEngine());
    }
    bool min = minimal;
    if (!bot->IsInCombat())
        min = true;
    if (bot && ai->HasRealPlayerMaster())
        min = false;
    ai->YieldAIInternalThread(min);
}

void StateNode_Legacy::UpdateStrategies(const std::string& name, BotState type)
{
    BotState botState = GetBotState(stateType);

    if (botState == BotState::BOT_STATE_ALL)
    {
        return;
    }

    if (type != BotState::BOT_STATE_ALL && botState != type)
    {
        return;
    }

    Engine* engine = ai->engines[(uint8)botState].get();

    engine->ChangeStrategy(name);
}


void StateNode_Legacy::ResetStrategies()
{
    BotState botState = GetBotState(stateType);

    if (botState == BotState::BOT_STATE_ALL)
    {
        return;
    }

    Engine* engine = ai->engines[(uint8)botState].get();

    engine->initMode = true;
    engine->removeAllStrategies();

    switch (stateType)
    {
        case ai::HLSStateType::NonCombat:
            AiFactory::AddDefaultNonCombatStrategies(bot, ai, engine);
        break;
        case ai::HLSStateType::Combat:
            AiFactory::AddDefaultCombatStrategies(bot, ai, engine);
        break;
        case ai::HLSStateType::Reaction:
            //AiFactory::AddDefaultReactionStrategies(bot, ai, engine);
        break;
        case ai::HLSStateType::Dead:
            AiFactory::AddDefaultDeadStrategies(bot, ai, engine);
        break;
        default: break;
    }

    engine->initMode = false;
    engine->Init();
}

void StateNode_Legacy::ReInit()
{
    BotState botState = GetBotState(stateType);

    if (botState == BotState::BOT_STATE_ALL)
    {
        return;
    }

    Engine* engine = ai->engines[(uint8)botState].get();

    engine->initMode = false;
    engine->Init();
}

StateNode_Reaction::StateNode_Reaction(PlayerbotAI* ai, Player* bot)
    : ai(ai), bot(bot)
{
    reactionEngine = AiFactory::createReactionEngine(bot, ai, ai->GetAiObjectContext());

    reactionEngine->initMode = false;
    reactionEngine->Init();
}

void StateNode_Reaction::OnExit()
{
    reactionEngine->Reset();
}

void StateNode_Reaction::UpdateStrategies(const std::string& name, BotState type)
{
    if (type == BotState::BOT_STATE_ALL || type == BotState::BOT_STATE_REACTION)
    {
        reactionEngine->ChangeStrategy(name);
    }
}

void StateNode_Reaction::ResetStrategies()
{
    reactionEngine->initMode = true;
    reactionEngine->removeAllStrategies();

    AiFactory::AddDefaultReactionStrategies(bot, ai, reactionEngine.get());

    reactionEngine->initMode = false;
    reactionEngine->Init();
}

void StateNode_Reaction::ReInit()
{ 
    reactionEngine->initMode = false;
    reactionEngine->Init();
    //ai->engines[(uint8)BotState::BOT_STATE_REACTION].get()->Init();
}

void StateNode_Reaction::Update(uint32 elapsed, bool minimal)
{
    bool doMinimalReaction = minimal;
    if (bot->IsTaxiFlying())
        doMinimalReaction = true;
    if (UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()))
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
    if (ai->UpdateAIInternal(elapsed, minimal))
    {
        //ai->DoNextAction(minimal, reactionEngine.get());
        ai->DoNextAction(minimal, ai->engines[(uint8)BotState::BOT_STATE_REACTION].get());
    }
    bool min = minimal;
    if (!bot->IsInCombat())
        min = true;
    if (bot && ai->HasRealPlayerMaster())
        min = false;
    ai->YieldAIInternalThread(min);
}

bool StateNode_Reaction::UpdateAIReaction(uint32 elapsed, bool minimal, bool isStunned)
{
    bool reactionFound;
    std::string mapString = WorldPosition(bot).isInstance() ? "I" : std::to_string(bot->GetMapId());

    auto pmo = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAIReaction " + mapString);
    
    const bool reactionInProgress = reactionEngine->Update(elapsed, minimal, isStunned, reactionFound);
    //const bool reactionInProgress = dynamic_cast<ai::ReactionEngine*>(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get())->Update(elapsed, minimal, isStunned, reactionFound);
    pmo.reset();

    if (reactionFound)
    {
        // If new reaction found force stop current actions (if required)
       // if (const Reaction* reaction = dynamic_cast<ai::ReactionEngine*>(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get())->GetReaction())
       if (const Reaction* reaction = reactionEngine->GetReaction())
        {
            if (reaction->ShouldInterruptCast())
            {
                ai->InterruptSpell();
            }

            if (reaction->ShouldInterruptMovement())
            {
                ai->StopMoving();
            }
        }
    }

    return reactionInProgress;
}

bool StateNode_Reaction::ShouldEnterReaction()
{
    return reactionEngine->HasIncomingReaction() || reactionEngine->FindReaction(bot->IsTaxiFlying());

    //return dynamic_cast<ai::ReactionEngine*>(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get())->FindReaction(bot->IsTaxiFlying());
}

bool StateNode_Reaction::IsReacting()
{
    return reactionEngine->IsReacting();
}