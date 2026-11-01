#pragma once

#include "playerbot/StateTree/Tasks/STAI_Task.h"
#include "playerbot/StateTree/STAI_TaskFactory.h"

#include "Entities/ObjectGuid.h"
#include "Entities/Unit.h"

#include <atomic>
#include <map>
#include <vector>

class Player;
class SpellAuraHolder;

namespace ai
{
    // An aura a target provider watches on its target: the configured spell and every rank of it
    struct STAI_WatchedAura
    {
        uint32 spellId;
        std::vector<uint32> ranks;
    };

    // Auras on the target of a target provider (TargetProvider_GlobalTask, TankTargetProvider_GlobalTask), configured
    // with <Aura spellid="7386"/> per aura (any rank, any caster). Update follows the provider's target: it subscribes
    // the target's aura events of the SpellAuraWorldSubsystem (applied / removed / stack changed; the previous
    // target's are unsubscribed) and reads the stacks again when the target changed or an event came. The callbacks
    // only set a flag: they run on the thread changing the target's auras.
    class STAI_TargetAuraWatch
    {
    public:
        ~STAI_TargetAuraWatch() { Unsubscribe(); }

        // the <Aura spellid="..."/> children of config, empty when there are none
        static std::vector<STAI_WatchedAura> Parse(const STAI_TaskConfig& config);

        // stacks[spellId] of each aura on target (the highest stack of any caster, 0 without the aura or a target);
        // true when an entry changed
        bool Update(Unit* target, const std::vector<STAI_WatchedAura>& auras, std::map<uint32, uint32>& stacks);
        void Unsubscribe();

    private:
        void OnAuraApplied(Unit* target, SpellAuraHolder* holder) { stale = true; }
        void OnAuraRemoved(Unit* target, SpellAuraHolder* holder, AuraRemoveMode mode) { stale = true; }
        void OnAuraStackChanged(Unit* target, SpellAuraHolder* holder, uint32 oldStackAmount, uint32 newStackAmount) { stale = true; }

        ObjectGuid subscribed;      // the target whose aura events are subscribed, empty when none
        std::atomic<bool> stale{ true };
    };
}
