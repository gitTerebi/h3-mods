#pragma once

#include <Plugin.hpp>
#include <H3API.hpp>

class AutoCombatAIPlugin : public Plugin
{
public:
	AutoCombatAIPlugin() : Plugin()
	{
		Status status = Init("H3.AutoCombatAI");
		if (status == Status::Success)
			Start();
	}

	static AutoCombatAIPlugin& GetPlugin();

	void ChooseCreatureAction(HiHook* h, h3::H3CombatManager* combat, INT32 side);
	INT32 ExecuteAction(HiHook* h, h3::H3CombatManager* combat, INT32 a1, INT32 a2);

private:
	void Start();
};
