#include "botpch.h"
#include "AutomationTest.h"
#include "TestWorld.h"
#include "PlayerbotTestHelpers.h"

#include "AI/BaseAI/UnitAI.h"
#include "Entities/Creature.h"
#include "Entities/Player.h"
#include "Maps/Map.h"
#include "World/ProfilingSubsystem.h"

#include <chrono>
#include <sstream>

BEGIN_DEFINE_SPEC(FMangosTestMageSpec,
"MangosTests.Playerbots.Mage")

uint32 Timeout = 15000; //15 secs

std::string const BotName = "Rottennerve";        // troll mage 60 of the characters database
uint32 const MobLevel = 60;
float const MobDistance = 20.0f;

Map* TestMap = nullptr;
ObjectGuid MobGuid;
// kept for the whole run: a bot login still running when a test ends calls back into it
std::unique_ptr<FPlayerbotTestHolder> Bots;
END_DEFINE_SPEC(FMangosTestMageSpec)

void FMangosTestMageSpec::Define()
{

    BeforeEach(Timeout, [this](const FDoneDelegate& DoneDelegate)
    {
        // create a simple map here, a plane
        TestMap = MangosTestWorld::CreateFlatMap();
        TestNotNull("flat test map", TestMap);

        // the counters printed by AfterEach cover this test only
        ProfilingSubsystem::ResetStats();

        DoneDelegate.Execute();
    });

    It("spawn a mob and a bot and let it go till timeout", Timeout, [this](const FDoneDelegate& OnDone)
    {
        if (!TestMap)
        {
            OnDone.Execute();
            return;
        }

        // spawn any non elite mob level 60 (once the bot is in the world, below)
        uint32 const mobEntry = MangosTestWorld::FindNormalHostileCreatureEntry(MobLevel);
        if (!TestTrue("a non elite level 60 hostile creature template exists", mobEntry != 0))
        {
            OnDone.Execute();
            return;
        }

        //spawn a bot, let's say Rottennerve (he is in the database, can be checked)
        if (!Bots)
            Bots = std::make_unique<FPlayerbotTestHolder>();

        std::string error;
        if (!Bots->Login(BotName, TestMap->GetId(), 0.0f, 0.0f, 0.0f, 0.0f, error))
        {
            AddError(error);
            OnDone.Execute();
            return;
        }

        // the fight runs until just before the timeout of the step
        auto const start = std::chrono::steady_clock::now();
        uint32 const runTime = Timeout > 1000 ? Timeout - 1000 : Timeout / 2;

        AddTicker([this, OnDone, mobEntry, start, runTime, botDied = false, mobDied = false](uint32 diff) mutable
        {
            Bots->Update(diff);

            uint32 const elapsed = uint32(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
            Player* bot = Bots->GetBot(BotName);

            if (bot && !MobGuid)
            {
                Creature* mob = MangosTestWorld::SpawnCreature(TestMap, mobEntry, MobDistance, 0.0f, 0.0f, M_PI_F, MobLevel);
                if (!mob)
                {
                    AddError("Could not spawn creature " + std::to_string(mobEntry));
                    OnDone.Execute();
                    return false;
                }

                MobGuid = mob->GetObjectGuid();
                AddInfo(std::string(mob->GetName()) + " (entry " + std::to_string(mobEntry) + ", level " + std::to_string(mob->GetLevel()) + ") attacks " +
                    bot->GetName() + " (level " + std::to_string(bot->GetLevel()) + ") after " + std::to_string(elapsed) + " ms");

                if (mob->AI())
                    mob->AI()->AttackStart(bot);
            }

            Creature* mob = MobGuid ? TestMap->GetCreature(MobGuid) : nullptr;
            if (mob && !mob->IsAlive() && !mobDied)
            {
                mobDied = true;
                AddInfo(std::string(mob->GetName()) + " died after " + std::to_string(elapsed) + " ms");
            }
            if (bot && !bot->IsAlive() && !botDied)
            {
                botDied = true;
                AddInfo(BotName + " died after " + std::to_string(elapsed) + " ms");
            }

            if (elapsed < runTime)
                return true;

            if (!bot)
                AddError(BotName + " did not enter the world within " + std::to_string(runTime) + " ms");
            else
            {
                std::ostringstream out;
                out << BotName << ": " << (bot->IsAlive() ? "alive" : "dead") << ", health " << bot->GetHealthPercent() << "%, mana "
                    << (bot->GetMaxPower(POWER_MANA) ? 100.0f * bot->GetPower(POWER_MANA) / bot->GetMaxPower(POWER_MANA) : 0.0f) << "%";
                if (mob)
                    out << "; " << mob->GetName() << ": " << (mob->IsAlive() ? "alive" : "dead") << ", health " << mob->GetHealthPercent() << "%";
                AddInfo(out.str());
            }

            OnDone.Execute();
            return false;
        });
    });

    AfterEach([this]()
    {
        //type the performance counters similar to the one we send to state bot tree
        std::istringstream report(ProfilingSubsystem::BuildTextReport());
        for (std::string line; std::getline(report, line);)
            AddInfo(line);

        MangosTestWorld::Despawn(TestMap, MobGuid);
        MobGuid.Clear();
        if (Bots)
            Bots->LogoutAll();
    });
}
