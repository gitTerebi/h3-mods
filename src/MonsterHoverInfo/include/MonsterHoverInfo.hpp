#pragma once

#include <Plugin.hpp>
#include <H3API.hpp>

class MonsterHoverInfoPlugin : public Plugin
{
public:
	MonsterHoverInfoPlugin() : Plugin()
	{
		Status status = Init("H3.MonsterHoverInfo");
		if (status == Status::Success)
			Start();
	}

	static MonsterHoverInfoPlugin& GetPlugin();

	int ShowMonsterHoverInfo(HookContext& c);
	int CaptureHotaCount(HookContext& c);

private:
	void Start();
};
