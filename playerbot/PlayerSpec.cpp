#include "playerbot/playerbot.h"
#include "playerbot/PlayerSpec.h"

int PlayerSpec::GetPlayerSpecTab(const Player* bot)
{
    std::map<uint32, int32> tabs = GetPlayerSpecTabs(bot);

    if (bot->GetLevel() >= 10 && ((tabs[0] + tabs[1] + tabs[2]) > 0))
    {
        int tab = -1, max = 0;
        for (uint32 i = 0; i < uint32(3); i++)
        {
            if (tab == -1 || max < tabs[i])
            {
                tab = i;
                max = tabs[i];
            }
        }
        return tab;
    }
    else
    {
        int tab = 0;

        switch (bot->getClass())
        {
        case CLASS_MAGE:
            tab = 1;
            break;
        case CLASS_PALADIN:
            tab = 2;
            break;
        case CLASS_PRIEST:
            tab = 1;
            break;
        case CLASS_WARRIOR:
            tab = 2;
            break;
        }
        return tab;
    }
}

std::map<uint32, int32> PlayerSpec::GetPlayerSpecTabs(const Player* bot)
{
    std::map<uint32, int32> tabs;
    for (uint32 i = 0; i < uint32(3); i++)
        tabs[i] = 0;

    uint32 classMask = bot->getClassMask();
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const *talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
            continue;

        TalentTabEntry const *talentTabInfo = sTalentTabStore.LookupEntry(talentInfo->TalentTab);
        if (!talentTabInfo)
            continue;

        if ((classMask & talentTabInfo->ClassMask) == 0)
            continue;

        int maxRank = 0;
        for (int rank = MAX_TALENT_RANK - 1; rank >= 0; --rank)
        {
            if (!talentInfo->RankID[rank])
                continue;

            uint32 spellid = talentInfo->RankID[rank];
            if (spellid && bot->HasSpell(spellid))
                maxRank = rank + 1;

        }
        tabs[talentTabInfo->tabpage] += maxRank;
    }

    return tabs;
}

BotRoles PlayerSpec::GetPlayerRoles(const Player* player)
{
    BotRoles role = BOT_ROLE_NONE;
    int tab = GetPlayerSpecTab(player);
    switch (player->getClass())
    {
        case CLASS_PRIEST:
        {
            if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }
            else
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

        case CLASS_SHAMAN:
        {
            if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_WARRIOR:
        {
            if (tab == 2 || player->HasAura(71)) // Defensive stance
            {
                role = BOT_ROLE_TANK;
            }
            else
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_PALADIN:
        {
            if (tab == 1 || player->HasAura(25780)) // Righteous fury
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 0)
            {
                role = BOT_ROLE_HEALER;  
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }

        case CLASS_DRUID:
        {
            if (player->HasAura(5487) || player->HasAura(9634)) // Bear form, Dire bear form
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 0)
            {
                role = BOT_ROLE_DPS;
            }
            else if (tab == 1)
            {
                role = BOT_ROLE_DPS;
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_HEALER;
            }

            break;
        }

#ifdef MANGOSBOT_TWO
        case CLASS_DEATH_KNIGHT:
        {
            if (player->HasAura(48263)) // Frost presence
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 0)
            {
                role = BOT_ROLE_TANK;
            }
            else if (tab == 1)
            {
                role = (BotRoles)(BOT_ROLE_TANK | BOT_ROLE_DPS);
            }
            else if (tab == 2)
            {
                role = BOT_ROLE_DPS;
            }

            break;
        }
#endif
        default:
        {
            role = BOT_ROLE_DPS;
            break;
        }
    }


    return role;
}
