
#include "playerbot/playerbot.h"
#include "AttackersValue.h"
#include "PossibleTargetsValue.h"
#include "EnemyPlayerValue.h"

using namespace ai;
using namespace MaNGOS;

std::list<ObjectGuid> AttackersValue::Get()
{
    const uint32 version = ai->GetTeamAttackersVersion();
    const uint32 tick = WorldTimer::tickTime();
    if (!built || version != builtVersion || tick != builtTick)
    {
        TRACE_CPU_PROFILER_STAT_SCOPE(GetProfilingStat("V: "));
        value = Build();
        built = true;
        builtVersion = version;
        builtTick = tick;
    }

    return value;
}

std::list<ObjectGuid> AttackersValue::Build()
{
    std::list<ObjectGuid> result;
    if (!ai->AllowActivity(ALL_ACTIVITY))
        return result;

    if (!bot->IsInWorld())
        return result;

    if (bot->IsBeingTeleported())
        return result;

    if (bot->IsFlying() && WorldPosition(bot).currentHeight() > 10.0f)
        return result;

    // lost control, e.g. BG ended
    if (bot->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_CLIENT_CONTROL_LOST))
        return result;

    if (ai->HasStrategy("focus rti targets", BotState::BOT_STATE_COMBAT))
    {
        Unit* rtiTarget = AI_VALUE(Unit*, "rti target");

        if (rtiTarget && rtiTarget->IsInWorld() && rtiTarget->GetMapId() == bot->GetMapId())
        {
            return { rtiTarget->GetObjectGuid() };
        }

        return result;
    }

    // Check if we only need one attacker
    const bool getOne = !qualifier.empty() && stoi(qualifier);

    std::set<Unit*> targets;

    // true once the list is complete (getOne)
    auto add = [&](Unit* unit) -> bool
    {
        if (!unit || targets.find(unit) != targets.end())
            return false;

        // a link (or the bot's own choice) says the unit fights the team: no in combat check
        if (!IsValid(unit, bot, bot, false))
            return false;

        targets.insert(unit);

        // Add the target's pet/guardian too
        unit->CallForAllControlledUnits(AddGuardiansHelper(targets), CONTROLLED_PET | CONTROLLED_GUARDIANS | CONTROLLED_CHARM | CONTROLLED_MINIPET | CONTROLLED_TOTEMS);
        return getOne;
    };

    // the bot's own targets: the fight it starts (attack, pull target), its duel, and the rti target of a bot
    // without an active player master
    std::vector<Unit*> ownTargets = {
        ai->GetUnit(AI_VALUE(ObjectGuid, "attack target")),
        AI_VALUE(Unit*, "pull target"),
        bot->duel ? bot->duel->opponent : nullptr
    };
    if (!ai->HasActivePlayerMaster())
        ownTargets.push_back(AI_VALUE(Unit*, "rti target"));

    bool done = false;
    for (Unit* unit : ownTargets)
        if ((done = add(unit)))
            break;

    // the units with a link to the bot, its pet or a group member near it (TeamSubsystem events)
    if (!done)
        for (const auto& attacker : ai->GetTeamAttackers())
            if (add(ai->GetUnit(attacker.first)))
                break;

    // Convert the targets to guids
    for (Unit* target : targets)
    {
        result.push_back(target->GetObjectGuid());
    }

    return result;
}

bool AttackersValue::InCombat(Unit* target, Player* player, bool checkPullTargets)
{
    // Check if the the target is attacking the player
    bool inCombat = (target->getThreatManager().getThreat(player) > 0.0f) ||
                    (target->GetVictim() && (target->GetVictim() == player));

    // Check if the target is attacking the player's pet
    if(!inCombat)
    {
        Pet* pet = player->GetPet();
        if (pet)
        {
            inCombat = (target->getThreatManager().getThreat(pet) > 0.0f) ||
                       (target->GetVictim() && (target->GetVictim() == pet));
        }
    }

    if(!inCombat && checkPullTargets && player->GetPlayerbotAI())
    {
        inCombat = (PAI_VALUE(ObjectGuid, "attack target") == target->GetObjectGuid()) ||
                   (PAI_VALUE(Unit*, "pull target") == target);
    }

    return inCombat;
}

bool AttackersValue::IsValid(Unit* target, Player* player, Player* owner, bool checkInCombat, bool validatePossibleTarget)
{
    Player* playerToCheckAgainst = owner != nullptr ? owner : player;

    // Validate possible target
    if (validatePossibleTarget && !PossibleTargetsValue::IsValid(target, playerToCheckAgainst))
    {
        return false;
    }

    // This will be used on both enemy player and npc checks
    const bool inPvPProhibitedZone = sPlayerbotAIConfig.IsInPvpProhibitedZone(sServerFacade.GetAreaId(target));

    // If the target is a player
    Player* enemyPlayer = dynamic_cast<Player*>(target);
    if (enemyPlayer)
    {
        // Don't consider enemy players if pvp strategy is not set
        if (playerToCheckAgainst->GetPlayerbotAI() && !playerToCheckAgainst->GetPlayerbotAI()->HasStrategy("pvp", BotState::BOT_STATE_COMBAT))
        {
            return false;
        }

        // If the enemy player is in a PVP Prohibited zone
        if (inPvPProhibitedZone)
        {
            return false;
        }

        // Don't check distance on duel opponents
        if (!player->duel || (player->duel && (player->duel->opponent != target)))
        {
            // If the enemy player is not within sight distance
            if (!enemyPlayer->IsWithinDist(playerToCheckAgainst, EnemyPlayerValue::GetMaxAttackDistance(playerToCheckAgainst), false))
            {
                return false;
            }
        }
    }
    // If the target is a NPC
    else
    {
        // If the target is not fighting the player (and if the owner bot is not pulling the target)
        if (checkInCombat && !InCombat(target, player, (player == owner)))
        {
            bool isRtiTarget = false;
            if (player->GetPlayerbotAI() && !player->GetPlayerbotAI()->HasActivePlayerMaster())
            {
                Unit* rtiTarget = PAI_VALUE(Unit*, "rti target");
                if (target == rtiTarget)
                    isRtiTarget = true;
            }

            if(!isRtiTarget)
                return false;
        }

        // If the target is a player's pet and in a PvP prohibited zone
        if (target->GetObjectGuid().IsPet() && inPvPProhibitedZone)
        {
            return false;
        }

        // If the target is evading
        const Creature* creature = dynamic_cast<Creature*>(target);
        if (creature)
        {
            if (creature->GetCombatManager().IsInEvadeMode())
            {
                return false;
            }
        }        
    }

    if (IgnoreTarget(target, playerToCheckAgainst))
        return false;

    return true;
}

bool AttackersValue::IgnoreTarget(Unit* target, Player* playerToCheckAgainst)
{
    if (!playerToCheckAgainst->GetPlayerbotAI())
        return false; 

    PlayerbotAI* ai = playerToCheckAgainst->GetPlayerbotAI();
    AiObjectContext* context = ai->GetAiObjectContext();

    //Ignore Hard hostiles while not already fighting.
    if (target->GetLevel() > (playerToCheckAgainst->GetLevel() + 5) && ai->GetState() == BotState::BOT_STATE_NON_COMBAT)
    {
        //When traveling a long distance.
        if (AI_VALUE(bool, "travel target traveling") && AI_VALUE2(float, "distance", "travel target") > sPlayerbotAIConfig.reactDistance)
            return true;

        //When moving to master far away.
        if (ai->HasStrategy("follow", BotState::BOT_STATE_NON_COMBAT) && AI_VALUE2(bool, "trigger active", "out of react range"))
            return true;

        if (ai->GetMaster() && !ai->HasActivePlayerMaster())
        {
            Player* player = ai->GetMaster();

            //When master is traveling a long distance.
            if (PAI_VALUE(bool, "travel target traveling") && PAI_VALUE2(float, "distance", "travel target") > sPlayerbotAIConfig.reactDistance)
                return true;
        }
    }

    Player* enemyPlayer = dynamic_cast<Player*>(target);

    if (!enemyPlayer)
    {
        bool isDummy = false;


        if (WorldPosition(playerToCheckAgainst).isOverworld() && target->AI() && target->AI()->IsPreventingDeath())
        {

            isDummy = true;
        }

        uint32 entry = target->GetEntry();

#define TRAINING_DUMMY_NPC_ENTRY1 190013
#define TRAINING_DUMMY_NPC_ENTRY2 190014
#define TRAINING_DUMMY_NPC_ENTRY3 190015
#define THERAMORE_COMBAT_DUMMY 4952
#define NAXXRAMAS_COMBAT_DUMMY 16211
#ifdef MANGOSBOT_TWO  
#define INITATES_TRAINING_DUMMY 32541
#endif

        switch (entry)
        {
        case TRAINING_DUMMY_NPC_ENTRY1:
        case TRAINING_DUMMY_NPC_ENTRY2:
        case TRAINING_DUMMY_NPC_ENTRY3:
        case THERAMORE_COMBAT_DUMMY:
        case NAXXRAMAS_COMBAT_DUMMY:
#ifdef MANGOSBOT_TWO     
        case INITATES_TRAINING_DUMMY:
#endif
            isDummy = true;
        }

        if (isDummy && ai->GetFixedBotNumber(BotTypeNumber::DUMMY_ATTACK_NUMBER, 10, 0.2f)) //90% of bots, cycle every 5 min.
            return true;
    }

    return false;
}

std::string AttackersValue::Format()
{
    std::ostringstream out;

    for (auto& target : value)
    {
        if(target != value.front())
        out << ",";

        WorldObject* wo = ai->GetWorldObject(target);

        if (wo)
            out << wo->GetName();
        else
            out << target;      
    }

    return out.str().c_str();
}

std::list<ObjectGuid> AttackersTargetingMeValue::Calculate()
{
    std::list<ObjectGuid> result;

    const std::list<ObjectGuid>& attackers = AI_VALUE(std::list<ObjectGuid>, "attackers");
    for (const ObjectGuid& attackerGuid : attackers)
    {
        Unit* attacker = ai->GetUnit(attackerGuid);
        if (attacker && (attacker->GetTarget() == bot || attacker->GetVictim() == bot))
        {
            result.push_back(attackerGuid);
        }
    }

    return result;
}
