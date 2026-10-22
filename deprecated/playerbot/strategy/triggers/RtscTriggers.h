#pragma once
#include "playerbot/strategy/Trigger.h"
#include "playerbot/strategy/NamedObjectContext.h"

namespace ai
{
	class RtscCloseTrigger : public Trigger, public Qualified
	{
	public:
		RtscCloseTrigger(PlayerbotAI* ai, std::string name = "is rtsc close") : Trigger(ai, name), Qualified() {}
		bool IsActive() override;
	};
}
