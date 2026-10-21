#pragma once
#include "playerbot/strategy/Value.h"
#include "playerbot/PlayerbotAIConfig.h"

namespace ai
{
    // List of hostile targets that are in combat with the bot (or bot group): the units with a link (threat list entry,
    // melee attack) to the bot, its pet or a group member near it, kept by PlayerbotAI from TeamSubsystem events
    // (GetTeamAttackers), plus the bot's own attack / pull target, duel opponent and rti target. Nothing is searched:
    // the list is only rebuilt from those (and validated, IsValid) when the links changed or a new world tick started.
    // Qualifier "1": the first valid one only.
    class AttackersValue : public UntypedValue, public Value<std::list<ObjectGuid>>, public Qualified
	{
        struct AddGuardiansHelper
        {
            explicit AddGuardiansHelper(std::set<Unit*>& units) : units(units) {}
            void operator()(Unit* target) const
            {
                units.insert(target);
            }

            std::set<Unit*>& units;
        };

	public:
        AttackersValue(PlayerbotAI* ai, std::string name = "attackers") : UntypedValue(ai, name), Qualified() {}

        virtual std::list<ObjectGuid> Get() override;
        virtual std::list<ObjectGuid> LazyGet() override { return Get(); }
        virtual void Set(std::list<ObjectGuid> value) override {}
        virtual void Reset() override { built = false; }

        static bool IsValid(Unit* target, Player* player, Player* owner = nullptr, bool checkInCombat = true, bool validatePossibleTarget = true);
        static bool IgnoreTarget(Unit* target, Player* playerToCheckAgainst);
        virtual std::string Format() override;

#ifdef GenerateBotHelp
        virtual std::string GetHelpName() { return "attackers"; } //Must equal iternal name
        virtual std::string GetHelpTypeName() { return "combat"; }
        virtual std::string GetHelpDescription()
        {
            return "This value contains all the units attacking the player, it's group or those the player or group is attacking.";
        }
        virtual std::vector<std::string> GetUsedValues() { return { "attack target", "pull target", "rti target" }; }
#endif

	private:
        std::list<ObjectGuid> Build();

        static bool InCombat(Unit* target, Player* player, bool checkPullTargets = true);

        std::list<ObjectGuid> value;
        bool built = false;
        uint32 builtVersion = 0;    // PlayerbotAI::GetTeamAttackersVersion
        uint32 builtTick = 0;       // WorldTimer::tickTime: validity (evading, visibility, ...) is checked again each tick
    };

    // List of attackers that are currently targeting the bot
    class AttackersTargetingMeValue : public ObjectGuidListCalculatedValue
    {
    public:
        AttackersTargetingMeValue(PlayerbotAI* ai, std::string name = "attackers targeting me") : ObjectGuidListCalculatedValue(ai, name) {}
        std::list<ObjectGuid> Calculate() override;
    };
}
