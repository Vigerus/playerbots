#pragma once
#include "StatsValues.h"

namespace ai
{
    // units that have the bot, its pet or a group member near it on their threat list (PlayerbotAI::GetTeamAttackerCount,
    // kept up to date by TeamSubsystem events, nothing is calculated here)
    class AttackersCountValue : public UntypedValue, public Value<uint8>, public Qualified
    {
    public:
        AttackersCountValue(PlayerbotAI* ai, std::string name = "attackers count") : UntypedValue(ai, name), Qualified() {}
        virtual uint8 Get() override;
        virtual uint8 LazyGet() override { return Get(); }
        virtual void Set(uint8 value) override {}
        virtual void Reset() override {}
        virtual std::string Format() override { return std::to_string(Get()); }
    };

    class PossibleAttackTargetsCountValue : public Uint8CalculatedValue, public Qualified
    {
    public:
        PossibleAttackTargetsCountValue(PlayerbotAI* ai, std::string name = "possible attack targets count") : Uint8CalculatedValue(ai, name, 4), Qualified() {}
        virtual uint8 Calculate();
    };

    // the bot or a group member near it fights: in combat, has attackers, or the bot announced a fight (pull, attack)
    // (PlayerbotAI::IsTeamFighting, kept up to date by TeamSubsystem events, nothing is calculated here)
    class HasAttackersValue : public UntypedValue, public Value<bool>, public Qualified
    {
    public:
        HasAttackersValue(PlayerbotAI* ai, std::string name = "has attackers") : UntypedValue(ai, name), Qualified() {}
        virtual bool Get() override;
        virtual bool LazyGet() override { return Get(); }
        virtual void Set(bool value) override {}
        virtual void Reset() override {}
        virtual std::string Format() override { return Get() ? "true" : "false"; }
    };

    class HasPossibleAttackTargetsValue : public BoolCalculatedValue, public Qualified
    {
    public:
        HasPossibleAttackTargetsValue(PlayerbotAI* ai, std::string name = "has possible attack targets") : BoolCalculatedValue(ai, name, 4), Qualified() {}
        virtual bool Calculate();
    };

    class MyAttackerCountValue : public Uint8CalculatedValue, public Qualified
    {
    public:
        MyAttackerCountValue(PlayerbotAI* ai, std::string name = "my attackers count") : Uint8CalculatedValue(ai, name, 4), Qualified() {}

        Unit* GetTarget()
        {
            AiObjectContext* ctx = AiObject::context;
            return ctx->GetValue<Unit*>(qualifier)->Get();
        }
        virtual uint8 Calculate();
    };

    class HasAggroValue : public BoolCalculatedValue, public Qualified
    {
    public:
        HasAggroValue(PlayerbotAI* ai, std::string name = "has agro") : BoolCalculatedValue(ai, name, 4), Qualified() {}

        Unit* GetTarget()
        {
            AiObjectContext* ctx = AiObject::context;
            return ctx->GetValue<Unit*>(qualifier)->Get();
        }
        virtual bool Calculate();
    };

    class BalancePercentValue : public Uint8CalculatedValue, public Qualified
    {
    public:
        BalancePercentValue(PlayerbotAI* ai, std::string name = "balance percentage") : Uint8CalculatedValue(ai, name, 4), Qualified() {}

        Unit* GetTarget()
        {
            AiObjectContext* ctx = AiObject::context;
            return ctx->GetValue<Unit*>(qualifier)->Get();
        }
        virtual uint8 Calculate();
    };

    class IncomingDamageValue : public Uint32CalculatedValue, public Qualified
    {
    public:
        IncomingDamageValue(PlayerbotAI* ai) : Uint32CalculatedValue(ai), Qualified() {}

        virtual uint32 Calculate()
        {
            Unit* target = AI_VALUE(Unit*, qualifier);
            if (!target)
                return 0;

            uint32 damage = 0;
            for (auto const& pAttacker : target->getAttackers())
                if (pAttacker->CanReachWithMeleeAttack(target))
                    damage += uint32((pAttacker->GetFloatValue(UNIT_FIELD_MINDAMAGE) + pAttacker->GetFloatValue(UNIT_FIELD_MAXDAMAGE)) / 2);

            return damage;
        }
    };
}
