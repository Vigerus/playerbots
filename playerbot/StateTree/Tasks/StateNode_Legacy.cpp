#include "StateNode_Legacy.h"

#include "playerbot/AiFactory.h"
#include "playerbot/PlayerbotLegacyStrategies.h"
#include "playerbot/StateTree/STAI_ActivityLog.h"
#include "playerbot/StateTree/Tasks/LegacyBrain.h"

#include <filesystem>

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

    // runs DoNextAction on the engine and logs the action it executed: from now until the bot is free again
    // (cast time / reaction delay the action set), ended earlier when the next action starts
    void DoNextActionLogged(PlayerbotAI* ai, Engine* engine, bool minimal, STAI_ActivityLog* activity, uint64& lastActionEntry)
    {
        const uint32 executedBefore = engine->GetExecutedActionCount();
        ai->DoNextAction(minimal, engine);

        if (!activity || engine->GetExecutedActionCount() == executedBefore)
            return;

        TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Legacy::ActivityLog);

        const ActionRunContext* context = engine->GetLastActionContext();
        if (!context)
            return;

        if (lastActionEntry)
            activity->Shorten(lastActionEntry);

        const uint32 now = WorldTimer::getMSTime();
        const uint32 busyMs = std::max(ai->GetAIInternalUpdateDelay(), 1U);
        lastActionEntry = activity->Add("action", context->name, context->strategy.empty() ? "-" : context->strategy, now, now + busyMs);
    }
}

// ---------------------------------------------------------------------------------------------------
// StateNode_Legacy
// ---------------------------------------------------------------------------------------------------

StateNode_Legacy::StateNode_Legacy(std::shared_ptr<const LegacyBrain> brain)
    : brain(std::move(brain))
{
}

void StateNode_Legacy::Validate(const STAI_TaskConfig& config)
{
    const std::string file = GetConfigFile(config);
    if (!file.empty())
        sLegacyBrainLibrary.Get(file); // throws with the parse error
}

std::unique_ptr<STAI_Task> StateNode_Legacy::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<StateNode_Legacy>(GetBrain(config));
}

Engine* StateNode_Legacy::GetEngine(const STAI_TaskContext& context)
{
    const BotState botState = GetBotState(context.state);
    if (botState == BotState::BOT_STATE_ALL)
        return nullptr;

    return context.ai->engines[(uint8)botState].get();
}

void StateNode_Legacy::ApplyBrain(PlayerbotAI* ai, Player* bot, Engine* engine, const LegacyBrain& brain)
{
    const uint32 mapKey = PlayerbotLegacyStrategies::GetMapKey(bot);

    engine->initMode = true;
    engine->removeAllStrategies();
    brain.Apply(bot, ai, engine);
    if (const PlayerbotLegacyStrategies::Changes* changes = ai->GetLegacyStrategies().Get(bot, brain.name, mapKey))
    {
        for (const std::string& strategy : changes->removed)
            engine->removeStrategy(strategy, false);
        for (const std::string& strategy : changes->added)
            engine->addStrategy(strategy);
    }
    engine->SetBrainMap(mapKey);
    engine->initMode = false;
    engine->Init();
}

void StateNode_Legacy::ApplyBrain(const STAI_TaskContext& context, Engine* engine) const
{
    ApplyBrain(context.ai, context.bot, engine, *brain);
}

bool StateNode_Legacy::NeedsBrain(const STAI_TaskContext& context, const Engine* engine) const
{
    return engine->GetBrainName() != brain->name || engine->GetBrainMap() != PlayerbotLegacyStrategies::GetMapKey(context.bot);
}

void StateNode_Legacy::InitContext(STAI_TaskContext& context) const
{
    if (brain)
        if (Engine* engine = GetEngine(context))
            ApplyBrain(context, engine);
}

void StateNode_Legacy::OnEnter(STAI_TaskContext& context) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Legacy::OnEnter);
    // several legacy nodes of one state share its engine: switch the strategy list when another one was applied
    // (or the bot's strategy changes of another map are)
    if (brain)
        if (Engine* engine = GetEngine(context))
            if (NeedsBrain(context, engine))
                ApplyBrain(context, engine);
}

EActionRunStatus StateNode_Legacy::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Legacy::OnTick);
    StateNode_LegacyContext& context = ContextAs<StateNode_LegacyContext>(baseContext);
    PlayerbotAI* ai = context.ai;
    Player* bot = context.bot;

    // the bot went outdoors / into another dungeon while the node runs: the strategy changes of the new map
    if (brain)
        if (Engine* engine = GetEngine(context))
            if (NeedsBrain(context, engine))
                ApplyBrain(context, engine);

    if (!ai->CanUpdateAIInternal())
        return EActionRunStatus::Running;
    // from here on the bot's legacy AI runs this tick (OnTick also counts the ticks that only wait)
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Legacy::OnTick::Update);
    Spell* currentSpell = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
    if (currentSpell && currentSpell->getState() == SPELL_STATE_CASTING && currentSpell->GetCastedTime() > 0U)
    {
        ai->SetAIInternalUpdateDelay(currentSpell->GetCastedTime() + sPlayerbotAIConfig.reactDelay + sWorld.GetAverageDiff());
        if (!ai->CanUpdateAIInternal())
            return EActionRunStatus::Running;
    }
    if (ai->UpdateAIInternal(elapsed, minimal))
    {
        DoNextActionLogged(ai, &ai->GetCurrentEngine(), minimal, context.activity, context.lastActionEntry);
    }
    bool min = minimal;
    if (!bot->IsInCombat())
        min = true;
    if (bot && ai->HasRealPlayerMaster())
        min = false;
    {
        TRACE_CPU_PROFILER_ENTRY_SCOPE(PlayerbotAI::YieldAIInternalThread);
        ai->YieldAIInternalThread(min);
    }
    return EActionRunStatus::Running;
}

void StateNode_Legacy::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Legacy::OnExit);
    StateNode_LegacyContext& context = ContextAs<StateNode_LegacyContext>(baseContext);

    // an interrupted action ends with the node
    if (context.lastActionEntry && context.activity)
        context.activity->Shorten(context.lastActionEntry);
    context.lastActionEntry = 0;
}

void StateNode_Legacy::UpdateStrategies(STAI_TaskContext& context, const std::string& name, BotState type) const
{
    const BotState botState = GetBotState(context.state);

    if (botState == BotState::BOT_STATE_ALL)
    {
        return;
    }

    if (type != BotState::BOT_STATE_ALL && botState != type)
    {
        return;
    }

    GetEngine(context)->ChangeStrategy(name);
}

void StateNode_Legacy::ResetStrategies(STAI_TaskContext& context) const
{
    Engine* engine = GetEngine(context);
    if (!engine)
    {
        return;
    }

    if (brain)
    {
        ApplyBrain(context, engine);
        return;
    }

    engine->initMode = true;
    engine->removeAllStrategies();

    switch (context.state)
    {
        case ai::HLSStateType::NonCombat:
            AiFactory::AddDefaultNonCombatStrategies(context.bot, context.ai, engine);
        break;
        case ai::HLSStateType::Combat:
            AiFactory::AddDefaultCombatStrategies(context.bot, context.ai, engine);
        break;
        case ai::HLSStateType::Reaction:
            //AiFactory::AddDefaultReactionStrategies(context.bot, context.ai, engine);
        break;
        case ai::HLSStateType::Dead:
            AiFactory::AddDefaultDeadStrategies(context.bot, context.ai, engine);
        break;
        default: break;
    }

    engine->initMode = false;
    engine->Init();
}

void StateNode_Legacy::ReInit(STAI_TaskContext& context) const
{
    Engine* engine = GetEngine(context);
    if (!engine)
    {
        return;
    }

    engine->initMode = false;
    engine->Init();
}

Engine* StateNode_Legacy::GetDebugEngine(const STAI_TaskContext& context) const
{
    return GetEngine(context);
}

// the strategy list's file name without path and extension (Legacy_NonCombat_Mage)
static std::string GetBrainDebugName(const std::shared_ptr<const LegacyBrain>& brain)
{
    return brain ? std::filesystem::path(brain->file).stem().string() : "AiFactory defaults";
}

std::string StateNode_Legacy::GetDebugInfo() const
{
    return GetBrainDebugName(brain);
}

// ---------------------------------------------------------------------------------------------------
// StateNode_Reaction
// ---------------------------------------------------------------------------------------------------

StateNode_ReactionContext::StateNode_ReactionContext() = default;
StateNode_ReactionContext::~StateNode_ReactionContext() = default;

bool StateNode_ReactionContext::ShouldEnterReaction()
{
    return reactionEngine->HasIncomingReaction() || reactionEngine->FindReaction(bot->IsTaxiFlying());

    //return dynamic_cast<ai::ReactionEngine*>(ai->engines[(uint8)BotState::BOT_STATE_REACTION].get())->FindReaction(bot->IsTaxiFlying());
}

bool StateNode_ReactionContext::IsReacting()
{
    return reactionEngine->IsReacting();
}

StateNode_Reaction::StateNode_Reaction(std::shared_ptr<const LegacyBrain> brain)
    : brain(std::move(brain))
{
}

std::unique_ptr<STAI_Task> StateNode_Reaction::Create(const STAI_TaskConfig& config)
{
    return std::make_unique<StateNode_Reaction>(GetBrain(config));
}

const STAI_TaskContextType& StateNode_Reaction::GetContextType() const
{
    return STAI_TaskContextType::Of<StateNode_ReactionContext>();
}

void StateNode_Reaction::ApplyBrain(const STAI_TaskContext& context, Engine* engine) const
{
    if (!engine)
        return;

    engine->initMode = true;
    engine->removeAllStrategies();
    brain->Apply(context.bot, context.ai, engine);
    engine->initMode = false;
    engine->Init();
}

void StateNode_Reaction::ApplyBrainOrDefaults(StateNode_ReactionContext& context) const
{
    if (brain)
    {
        ApplyBrain(context, context.reactionEngine.get());
        ApplyBrain(context, context.ai->engines[(uint8)BotState::BOT_STATE_REACTION].get());
        return;
    }

    context.reactionEngine->initMode = true;
    context.reactionEngine->removeAllStrategies();

    AiFactory::AddDefaultReactionStrategies(context.bot, context.ai, context.reactionEngine.get());

    context.reactionEngine->initMode = false;
    context.reactionEngine->Init();
}

void StateNode_Reaction::InitContext(STAI_TaskContext& baseContext) const
{
    StateNode_ReactionContext& context = ContextAs<StateNode_ReactionContext>(baseContext);
    context.reactionEngine = AiFactory::createReactionEngine(context.bot, context.ai, context.ai->GetAiObjectContext());

    if (brain)
    {
        ApplyBrainOrDefaults(context);
        return;
    }

    // createReactionEngine added the defaults already
    context.reactionEngine->initMode = false;
    context.reactionEngine->Init();
}

void StateNode_Reaction::OnExit(STAI_TaskContext& baseContext) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Reaction::OnExit);
    StateNode_ReactionContext& context = ContextAs<StateNode_ReactionContext>(baseContext);

    context.reactionEngine->Reset();

    if (context.lastActionEntry && context.activity)
        context.activity->Shorten(context.lastActionEntry);
    context.lastActionEntry = 0;
}

void StateNode_Reaction::UpdateStrategies(STAI_TaskContext& baseContext, const std::string& name, BotState type) const
{
    if (type == BotState::BOT_STATE_ALL || type == BotState::BOT_STATE_REACTION)
    {
        ContextAs<StateNode_ReactionContext>(baseContext).reactionEngine->ChangeStrategy(name);
    }
}

void StateNode_Reaction::ResetStrategies(STAI_TaskContext& baseContext) const
{
    ApplyBrainOrDefaults(ContextAs<StateNode_ReactionContext>(baseContext));
}

void StateNode_Reaction::ReInit(STAI_TaskContext& baseContext) const
{
    StateNode_ReactionContext& context = ContextAs<StateNode_ReactionContext>(baseContext);
    context.reactionEngine->initMode = false;
    context.reactionEngine->Init();
    //ai->engines[(uint8)BotState::BOT_STATE_REACTION].get()->Init();
}

EActionRunStatus StateNode_Reaction::OnTick(STAI_TaskContext& baseContext, uint32 elapsed, bool minimal) const
{
    TRACE_CPU_PROFILER_ENTRY_SCOPE(StateNode_Reaction::OnTick);
    StateNode_ReactionContext& context = ContextAs<StateNode_ReactionContext>(baseContext);
    PlayerbotAI* ai = context.ai;
    Player* bot = context.bot;

    bool doMinimalReaction = minimal;
    if (bot->IsTaxiFlying())
        doMinimalReaction = true;
    if (UpdateAIReaction(context, elapsed, doMinimalReaction, bot->IsTaxiFlying()))
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
        //ai->DoNextAction(minimal, context.reactionEngine.get());
        DoNextActionLogged(ai, ai->engines[(uint8)BotState::BOT_STATE_REACTION].get(), minimal, context.activity, context.lastActionEntry);
    }
    bool min = minimal;
    if (!bot->IsInCombat())
        min = true;
    if (bot && ai->HasRealPlayerMaster())
        min = false;
    ai->YieldAIInternalThread(min);
    return EActionRunStatus::Running;
}

bool StateNode_Reaction::UpdateAIReaction(StateNode_ReactionContext& context, uint32 elapsed, bool minimal, bool isStunned)
{
    PlayerbotAI* ai = context.ai;
    ReactionEngine* reactionEngine = context.reactionEngine.get();

    bool reactionFound;
    std::string mapString = WorldPosition(context.bot).isInstance() ? "I" : std::to_string(context.bot->GetMapId());

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

IReactionChecker* StateNode_Reaction::GetReactionChecker(STAI_TaskContext& context) const
{
    return &ContextAs<StateNode_ReactionContext>(context);
}

Engine* StateNode_Reaction::GetDebugEngine(const STAI_TaskContext& context) const
{
    return ContextAs<StateNode_ReactionContext>(context).reactionEngine.get();
}

std::string StateNode_Reaction::GetDebugInfo() const
{
    return GetBrainDebugName(brain);
}
