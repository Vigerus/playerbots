
#include "playerbot/playerbot.h"
#include "playerbot/Helpers.h"
#include "RtiAction.h"
#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/strategy/values/RtiTargetValue.h"
#include "playerbot/strategy/values/RtiValue.h"

using namespace ai;

bool RtiAction::Execute(Event& event)
{
    Player* requester = event.getOwner() ? event.getOwner() : GetMaster();
    std::string text = event.getParam();
    std::string type = "rti";
    if (text.find("cc ") == 0)
    {
        type = "rti cc";
        text = text.substr(3);
    }
    else if (text.empty() || text == "?")
    {
        std::ostringstream outRti; outRti << "rti" << ": ";
        AppendRti(outRti, "rti");
        ai->TellPlayer(requester, outRti);

        std::ostringstream outRtiCc; outRtiCc << "rti cc" << ": ";
        AppendRti(outRtiCc, "rti cc");
        ai->TellPlayer(requester, outRtiCc);
        return true;
    }

    // "rti skull,cross,square": up to 8 icons, the first marked target in this order is attacked; "rti none" clears it
    std::vector<std::string> invalid;
    std::vector<std::string> rti_list = ParseRtiList(text, &invalid);
    if (rti_list.empty() && text != "none")
    {
        std::ostringstream out;
        out << "Usage: " << type << " <icon>[,<icon>...] (up to " << MAX_RTI_ICONS << ", in priority order) or " << type
            << " none; icons: star, circle, diamond, triangle, moon, square, cross, skull";
        ai->TellError(requester, out.str());
        return false;
    }

    context->GetValue<std::vector<std::string>>(type)->Set(rti_list);

    std::ostringstream out; out << type << " set to: ";
    AppendRti(out, type);
    if (!invalid.empty())
        out << " (unknown: " << FormatRtiList(invalid) << ")";
    ai->TellPlayer(requester, out);
    return true;
}

void RtiAction::AppendRti(std::ostringstream & out, std::string type)
{
    out << FormatRtiList(AI_VALUE(std::vector<std::string>, type));

    std::ostringstream n; n << type << " target";

    Unit* target = AI_VALUE(Unit*, n.str());
    if (target)
        out << " (" << target->GetName() << ")";
}

bool MarkRtiAction::Execute(Event& event)
{
    Group *group = bot->GetGroup();
    if (!group) return false;

    if (bot->InBattleGround())
        return false;

    Unit* target = NULL;
    std::list<ObjectGuid> attackers = ai->GetAiObjectContext()->GetValue<std::list<ObjectGuid>>("possible attack targets")->Get();
    for (std::list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); ++i)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit)
            continue;

        // do not mark players
        if (unit->IsPlayer())
            continue;

        bool marked = false;
        for (int i = 0; i < 8; i++)
        {
            ObjectGuid guid = group->GetTargetIcon(i);
            if (guid == unit->GetObjectGuid())
            {
                marked = true;
                break;
            }
        }

        if (marked) continue;

        if (!target || (int)target->GetHealth() > (int)unit->GetHealth()) target = unit;
    }

    if (!target) return false;

    std::vector<std::string> rti_list = AI_VALUE(std::vector<std::string>, "rti");
    if (rti_list.size())
    {
        int index = RtiTargetValue::GetRtiIndex(rti_list[0]);
#ifndef MANGOSBOT_TWO
        group->SetTargetIcon(index, target->GetObjectGuid());
#else
        group->SetTargetIcon(index, bot->GetObjectGuid(), target->GetObjectGuid());
#endif
        return true;
    }

    return false;
}

