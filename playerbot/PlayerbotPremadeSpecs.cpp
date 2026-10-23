#include "PlayerbotPremadeSpecs.h"

#include "playerbot/PlayerbotAIConfig.h"
#include "playerbot/StateTree/Providers/SpecProvider_GlobalTask.h"

#include "Entities/Player.h"

using namespace ai;

const std::vector<TalentPath>& PlayerbotPremadeSpecs::GetPaths(const Player* bot)
{
    static const std::vector<TalentPath> none;
    const uint8 cls = bot->getClass();
    return cls < MAX_CLASSES ? sPlayerbotAIConfig.classSpecs[cls].talentPath : none;
}

const TalentPath* PlayerbotPremadeSpecs::Find(const Player* bot, int pathId)
{
    for (const TalentPath& path : GetPaths(bot))
        if (path.id == pathId)
            return &path;
    return nullptr;
}

TalentSpec PlayerbotPremadeSpecs::GetSpecForLevel(Player* bot, const TalentPath& path)
{
    const uint32 points = bot->CalculateTalentsPoints();
    const TalentSpec* chosen = &path.talentSpec.back();
    for (const TalentSpec& spec : path.talentSpec)
    {
        if (spec.points >= points)
        {
            chosen = &spec;
            break;
        }
    }

    TalentSpec spec = *chosen;
    spec.CropTalents(bot);
    return spec;
}

int PlayerbotPremadeSpecs::GetCurrentPathId(Player* bot)
{
    const std::vector<TalentPath>& paths = GetPaths(bot);
    if (paths.empty())
        return -1;

    // exactly the talents of a path
    const std::string botLink = TalentSpec(bot).GetTalentLink();
    for (const TalentPath& path : paths)
        if (!path.talentSpec.empty() && GetSpecForLevel(bot, path).GetTalentLink() == botLink)
            return path.id;

    // else the only path that makes the bot's spec
    const std::string specName = SpecProvider_GlobalTask::GetSpecName(bot);
    int found = -1;
    for (const TalentPath& path : paths)
    {
        if (path.hint != specName)
            continue;
        if (found >= 0)
            return -1;
        found = path.id;
    }
    return found;
}

bool PlayerbotPremadeSpecs::Apply(Player* bot, int pathId, std::ostringstream& out)
{
    const TalentPath* path = Find(bot, pathId);
    if (!path || path->talentSpec.empty())
        return false;

    // learning and unlearning the talent spells fires the core's OnTalentsChanged (SpecProvider_GlobalTask checks
    // the spec again, onSpecChanged)
    TalentSpec spec = GetSpecForLevel(bot, *path);
    spec.ApplyTalents(bot, &out);
    out << "Applied spec " << path->name << " (" << spec.formatSpec(bot) << ")";
    return true;
}
