#include "StateNode_Legacy.h"

#include "playerbot/AiFactory.h"
#include "playerbot/StateTree/Tasks/LegacyBrain.h"

namespace
{
    BotState GetBotState(HLSStateType stateType)
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

    // <Config filename="..."/>, empty when the task has no config
    std::string GetConfigFile(const STAI_TaskConfig& config)
    {
        return STAI_TaskFactory::GetSetting(config, "Config", "filename").value_or("");
    }

    std::shared_ptr<const LegacyBrain> GetBrain(const STAI_TaskConfig& config)
    {
        const std::string file = GetConfigFile(config);
        if (file.empty())
            return nullptr;

        try
        {
            return sLegacyBrainLibrary.Get(file);
        }
        catch (const std::exception& error)
        {
            sLog.outError("StateTree: %s not loaded, using AiFactory defaults: %s", file.c_str(), error.what());
            return nullptr;
        }
    }
}

// ---------------------------------------------------------------------------------------------------
// StateNode_Legacy
// ---------------------------------------------------------------------------------------------------

StateNode_Legacy::StateNode_Legacy(const STAI_TaskContext& context, std::shared_ptr<const LegacyBrain> brain)
    : ai(context.ai), bot(context.bot), stateType(context.state), brain(std::move(brain))
{
    if (this->brain)
        if (Engine* engine = GetEngine())
            ApplyBrain(engine);
}

void StateNode_Legacy::Validate(const STAI_TaskConfig& config)
{
    const std::string file = GetConfigFile(config);
    if (!file.empty())
        sLegacyBrainLibrary.Get(file); // throws with the parse error
}

std::unique_ptr<STAI_Task> StateNode_Legacy::Create(const STAI_TaskContext& context, const STAI_TaskConfig& config)
{
    return std::make_unique<StateNode_Legacy>(context, GetBrain(config));
}

Engine* StateNode_Legacy::GetEngine() const
{
    const BotState botState = GetBotState(stateType);
    if (botState == BotState::BOT_STATE_ALL)
        return nullptr;

    return ai->engines[(uint8)botState].get();
}

void StateNode_Legacy::ApplyBrain(Engine* engine)
{
    engine->initMode = true;
    engine->removeAllStrategies();
    brain->Apply(bot, ai, engine);
    engine->initMode = false;
    engine->Init();
}

void StateNode_Legacy::OnEnter()
{
    // several legacy nodes of one state share its engine: switch the strategy list when another one was applied
    if (brain)
        if (Engine* engine = GetEngine())
            if (engine->GetBrainName() != brain->name)
                ApplyBrain(engine);
}

EActionRunStatus StateNode_Legacy::Update(uint32 elapsed, bool minimal)
{
    if (!ai->CanUpdateAIInternal())
        return EActionRunStatus::Running;
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && currentSpell->getState() == SPELL_STATE_CASTING && currentSpell->GetCastedTime() > 0U)
    {
        ai->SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());
        if (!ai->CanUpdateAIInternal())
            return EActionRunStatus::Running;
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
    return EActionRunStatus::Running;
}

void StateNode_Legacy::UpdateStrategies(const std::string& name, BotState type)
{
    const BotState botState = GetBotState(stateType);

    if (botState == BotState::BOT_STATE_ALL)
    {
        return;
    }

    if (type != BotState::BOT_STATE_ALL && botState != type)
    {
        return;
    }

    GetEngine()->ChangeStrategy(name);
}

void StateNode_Legacy::ResetStrategies()
{
    Engine* engine = GetEngine();
    if (!engine)
    {
        return;
    }

    if (brain)
    {
        ApplyBrain(engine);
        return;
    }

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
    Engine* engine = GetEngine();
    if (!engine)
    {
        return;
    }

    engine->initMode = false;
    engine->Init();
}

Engine* StateNode_Legacy::GetDebugEngine() const
{
    return GetEngine();
}

std::string StateNode_Legacy::GetDebugInfo() const
{
    return brain ? brain->file : "AiFactory defaults";
}

// ---------------------------------------------------------------------------------------------------
// StateNode_Reaction
// ---------------------------------------------------------------------------------------------------

StateNode_Reaction::StateNode_Reaction(const STAI_TaskContext& context, std::shared_ptr<const LegacyBrain> brain)
    : ai(context.ai), bot(context.bot), brain(std::move(brain))
{
    reactionEngine = AiFactory::createReactionEngine(bot, ai, ai->GetAiObjectContext());

    if (this->brain)
    {
        ApplyBrain(reactionEngine.get());
        ApplyBrain(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get());
        return;
    }

    reactionEngine->initMode = false;
    reactionEngine->Init();
}

std::unique_ptr<STAI_Task> StateNode_Reaction::Create(const STAI_TaskContext& context, const STAI_TaskConfig& config)
{
    return std::make_unique<StateNode_Reaction>(context, GetBrain(config));
}

void StateNode_Reaction::ApplyBrain(Engine* engine)
{
    if (!engine)
        return;

    engine->initMode = true;
    engine->removeAllStrategies();
    brain->Apply(bot, ai, engine);
    engine->initMode = false;
    engine->Init();
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
    if (brain)
    {
        ApplyBrain(reactionEngine.get());
        ApplyBrain(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get());
        return;
    }

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

EActionRunStatus StateNode_Reaction::Update(uint32 elapsed, bool minimal)
{
    bool doMinimalReaction = minimal;
    if (bot->IsTaxiFlying())
        doMinimalReaction = true;
    if (UpdateAIReaction(elapsed, doMinimalReaction, bot->IsTaxiFlying()))
    {
        return EActionRunStatus::Running;
    }
    if (!ai->CanUpdateAIInternal())
        return EActionRunStatus::Running;
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && currentSpell->getState() == SPELL_STATE_CASTING && currentSpell->GetCastedTime() > 0U)
    {
        ai->SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());
        if (!ai->CanUpdateAIInternal())
            return EActionRunStatus::Running;
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
    return EActionRunStatus::Running;
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

Engine* StateNode_Reaction::GetDebugEngine() const
{
    return reactionEngine.get();
}

std::string StateNode_Reaction::GetDebugInfo() const
{
    return brain ? brain->file : "AiFactory defaults";
}
