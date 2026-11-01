#pragma once

#include <H3API.hpp>

namespace planner
{
	constexpr INT32 HEX_COUNT = 187;
	constexpr INT32 STACKS_PER_SIDE = 21;

	enum class Kind
	{
		Defend,
		Wait,
		Walk,
		Attack,
		Shoot,
	};

	struct Decision
	{
		Kind kind = Kind::Defend;
		INT32 hex = -1;
		h3::H3CombatMonster* target = nullptr;
		double score = 0.0;
		double gained = 0.0;
		double lost = 0.0;
		CHAR alternatives[256] = {};
		CHAR trace[2048] = {};
	};

	struct Context
	{
		INT32 stalledRounds = 0;
	};

	Decision Choose(h3::H3CombatManager& combat, INT32 side, h3::H3CombatMonster& actor, const Context& context);
	LPCSTR KindName(Kind kind);
	void PredictStockTurn(h3::H3CombatManager& combat, h3::H3CombatMonster& actor, PCHAR out, size_t size);
}
