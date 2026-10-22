#include "playerbot/playerbot.h"
#include "playerbot/ItemQualifier.h"

using namespace ai;

ItemQualifier::ItemQualifier(std::string qualifier, bool linkQualifier)
{
    itemId = 0;
    enchantId = 0;
    randomPropertyId = 0;
    gem1 = 0;
    gem2 = 0;
    gem3 = 0;
    gem4 = 0;
    proto = nullptr;

    std::vector<std::string> numbers;
    size_t last = 0, next = 0;
    while ((next = qualifier.find(':', last)) != std::string::npos)
    {
        numbers.push_back(qualifier.substr(last, next - last));
        last = next + 1;
    }
    numbers.push_back(qualifier.substr(last));

    if (numbers[0].empty())
        return;

    for (char& d : numbers[0]) //Check if itemId contains only numbers
        if (!isdigit(d))
            return;

    itemId = stoi(numbers[0]);

#ifdef MANGOSBOT_ZERO
    uint32 propertyPosition = linkQualifier ? 2 : 6;
#else
    uint32 propertyPosition = linkQualifier ? 6 : 6;
#endif

    if (numbers.size() > 1 && !numbers[1].empty())
        enchantId = stoi(numbers[1]);

    if (numbers.size() > propertyPosition && !numbers[propertyPosition].empty())
        randomPropertyId = stoi(numbers[propertyPosition]);

#ifndef MANGOSBOT_ZERO
    uint8 gemPosition = linkQualifier ? 2 : 2;

    if (numbers.size() > gemPosition + 3)
    {
        if (!numbers[gemPosition].empty())
            gem1 = stoi(numbers[gemPosition]);
        if (!numbers[gemPosition + 1].empty())
            gem2 = stoi(numbers[gemPosition + 1]);
        if (!numbers[gemPosition + 2].empty())
            gem3 = stoi(numbers[gemPosition + 2]);
        if (!numbers[gemPosition + 3].empty())
            gem4 = stoi(numbers[gemPosition + 3]);
    }
#endif
}

uint32 ItemQualifier::GemId(Item* item, uint8 gemSlot)
{
#ifdef MANGOSBOT_ZERO
    return 0;
#else
    uint32 enchantId = item->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + gemSlot));

    if (!enchantId)
        return 0;

    return enchantId;
#endif
}
