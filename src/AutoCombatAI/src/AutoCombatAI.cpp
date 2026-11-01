#include "AutoCombatAI.hpp"
#include "Planner.hpp"

#include <cstdarg>
#include <cstdio>
#include <vector>

using namespace h3;

AutoCombatAIPlugin AutoCombatAI;

AutoCombatAIPlugin& AutoCombatAIPlugin::GetPlugin()
{
	return AutoCombatAI;
}

namespace
{
	constexpr DWORD AI_CHOOSE_CREATURE_ACTION = 0x422370;
	const BYTE kStockChooseActionStart[16] = { 0x56, 0x8B, 0xF1, 0x57, 0x6A, 0x01, 0xC7, 0x86, 0xC8, 0x32, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };

	constexpr DWORD CLEAR_HOVER_HIGHLIGHT = 0x477740;
	constexpr DWORD EXECUTE_ACTION = 0x4786B0;
	constexpr DWORD STOCK_SCORE_STAND_HEX = 0x436490;
	constexpr DWORD STOCK_ATTACK_VALUE = 0x4358C0;
	constexpr DWORD STOCK_RANDOM_IN_RANGE = 0x50C7C0;
	constexpr DWORD STOCK_DANGER_MAP = 0x4203E0;
	constexpr DWORD STOCK_AFTER_TARGET_ROLL = 0x421C87;
	const BYTE kStockAfterTargetRoll[7] = { 0x8B, 0xF8, 0xB8, 0x1F, 0x85, 0xEB, 0x51 };
	constexpr INT32 STOCK_ROLL_TARGET_FRAME_OFFSET = -0xC;
	constexpr INT32 EVALUATOR_ATTACKER = 0;
	constexpr INT32 EVALUATOR_TARGET = 2;
	constexpr INT32 EVALUATOR_HEX_VALUE = 7;
	constexpr INT32 EVALUATOR_STAND = 8;
	constexpr INT32 EVALUATOR_TURNS = 9;
	constexpr INT32 STACK_AI_FIELD_A = 0x10;
	constexpr INT32 STACK_AI_FIELD_B = 0x14;

	using planner::HEX_COUNT;
	using planner::STACKS_PER_SIDE;
	constexpr INT32 FAR_AWAY = 1000;

	constexpr DWORD GAME_THREAD_DATA = 0x61D8C3;
	constexpr INT32 THREAD_DATA_RAND_SEED = 0x14;
	constexpr DWORD COMBAT_PATHFINDER = 0x6992D4;
	constexpr INT32 PATHFINDER_NODES = 0x24;
	constexpr size_t PATHFINDER_NODES_SIZE = 0x15EA;
	constexpr INT32 PATHFINDER_QUEUE = 0x3C;
	constexpr INT32 PATHFINDER_ROUTE = 0x4C;
	constexpr INT32 PATHFINDER_STOP_HEXES = 0x68;

	struct GameVector
	{
		BYTE* begin;
		BYTE* end;
		BYTE* capacity;
	};

	class GameSideEffects
	{
	public:
		explicit GameSideEffects(H3CombatManager& combat)
			: combat_(combat), seed_(*RandSeed()), pathfinder_(*reinterpret_cast<BYTE**>(COMBAT_PATHFINDER))
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				leftMarks_[hex] = combat.squares[hex].availableForLeftSquare;
				rightMarks_[hex] = combat.squares[hex].availableForRightSquare;
			}
			if (!pathfinder_)
				return;
			nodes_ = Field<BYTE*>(PATHFINDER_NODES);
			stopHexes_ = Field<BYTE*>(PATHFINDER_STOP_HEXES);
			if (nodes_)
				savedNodes_.assign(nodes_, nodes_ + PATHFINDER_NODES_SIZE);
			if (stopHexes_)
				savedStopHexes_.assign(stopHexes_, stopHexes_ + HEX_COUNT);
			queue_ = Field<GameVector>(PATHFINDER_QUEUE);
			route_ = Field<GameVector>(PATHFINDER_ROUTE);
			if (queue_.begin)
				savedQueue_.assign(queue_.begin, queue_.end);
			if (route_.begin)
				savedRoute_.assign(route_.begin, route_.end);
		}

		void Restore()
		{
			*RandSeed() = seed_;
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				combat_.squares[hex].availableForLeftSquare = leftMarks_[hex];
				combat_.squares[hex].availableForRightSquare = rightMarks_[hex];
			}
			if (!pathfinder_ || pathfinder_ != *reinterpret_cast<BYTE**>(COMBAT_PATHFINDER))
				return;
			if (nodes_ && nodes_ == Field<BYTE*>(PATHFINDER_NODES))
				memcpy(nodes_, savedNodes_.data(), savedNodes_.size());
			if (stopHexes_ && stopHexes_ == Field<BYTE*>(PATHFINDER_STOP_HEXES))
				memcpy(stopHexes_, savedStopHexes_.data(), savedStopHexes_.size());
			RestoreVector(PATHFINDER_QUEUE, queue_, savedQueue_);
			RestoreVector(PATHFINDER_ROUTE, route_, savedRoute_);
		}

	private:
		static DWORD* RandSeed()
		{
			return reinterpret_cast<DWORD*>(CDECL_0(BYTE*, GAME_THREAD_DATA) + THREAD_DATA_RAND_SEED);
		}

		template <typename T>
		T& Field(INT32 offset)
		{
			return *reinterpret_cast<T*>(pathfinder_ + offset);
		}

		void RestoreVector(INT32 offset, const GameVector& saved, const std::vector<BYTE>& content)
		{
			GameVector& now = Field<GameVector>(offset);
			if (now.begin != saved.begin || now.capacity != saved.capacity)
				return;
			if (!content.empty())
				memcpy(saved.begin, content.data(), content.size());
			now.end = saved.end;
		}

		H3CombatManager& combat_;
		DWORD seed_;
		BYTE* pathfinder_;
		BOOL8 leftMarks_[HEX_COUNT];
		BOOL8 rightMarks_[HEX_COUNT];
		BYTE* nodes_ = nullptr;
		BYTE* stopHexes_ = nullptr;
		GameVector queue_ = {};
		GameVector route_ = {};
		std::vector<BYTE> savedNodes_;
		std::vector<BYTE> savedStopHexes_;
		std::vector<BYTE> savedQueue_;
		std::vector<BYTE> savedRoute_;
	};

	LPCSTR LogPath()
	{
		static CHAR path[MAX_PATH] = {};
		static bool ready = false;
		if (ready)
			return path;
		ready = true;
		GetModuleFileNameA(nullptr, path, MAX_PATH);
		PCHAR slash = strrchr(path, '\\');
		if (slash)
			*(slash + 1) = 0;
		else
			path[0] = 0;

		CHAR owner[MAX_PATH + 32] = "AutoCombatAI.log owner ";
		strcat_s(owner, path);
		for (PCHAR c = owner; *c; ++c)
			if (*c == '\\')
				*c = '/';
		CreateMutexA(nullptr, FALSE, owner);
		if (GetLastError() == ERROR_ALREADY_EXISTS)
		{
			CHAR name[48] = {};
			sprintf_s(name, "AutoCombatAI.%lu.log", GetCurrentProcessId());
			strcat_s(path, name);
		}
		else
			strcat_s(path, "AutoCombatAI.log");
		return path;
	}

	void Log(LPCSTR format, ...)
	{
		LPCSTR path = LogPath();
		FILE* f = nullptr;
		if (fopen_s(&f, path, "a") != 0 || !f)
			return;
		va_list args;
		va_start(args, format);
		vfprintf(f, format, args);
		va_end(args);
		fputc('\n', f);
		fclose(f);
	}

	bool IsAlive(const H3CombatMonster& stack)
	{
		return stack.type >= 0 && stack.numberAlive > 0;
	}

	bool IsFighter(const H3CombatMonster& stack)
	{
		return IsAlive(stack) && !stack.info.flags.siegeWeapon;
	}

	INT32 OccupiedHexes(H3CombatMonster& stack, INT32 (&hexes)[2])
	{
		INT32 count = 0;
		if (stack.position >= 0 && stack.position < HEX_COUNT)
			hexes[count++] = stack.position;
		if (stack.info.flags.doubleWide)
		{
			const INT32 second = stack.GetSecondSquare();
			if (second >= 0 && second < HEX_COUNT && second != stack.position)
				hexes[count++] = second;
		}
		return count;
	}

	INT32 DistanceIgnoringObstacles(H3CombatManager& combat, H3CombatMonster& from, H3CombatMonster& to)
	{
		bool isTarget[HEX_COUNT] = {};
		INT32 targetHexes[2];
		const INT32 targetCount = OccupiedHexes(to, targetHexes);
		for (INT32 i = 0; i < targetCount; ++i)
			isTarget[targetHexes[i]] = true;

		INT32 depth[HEX_COUNT];
		for (INT32 i = 0; i < HEX_COUNT; ++i)
			depth[i] = -1;
		INT32 queue[HEX_COUNT];
		INT32 head = 0;
		INT32 tail = 0;

		INT32 startHexes[2];
		const INT32 startCount = OccupiedHexes(from, startHexes);
		for (INT32 i = 0; i < startCount; ++i)
		{
			depth[startHexes[i]] = 0;
			queue[tail++] = startHexes[i];
		}

		while (head < tail)
		{
			const INT32 hex = queue[head++];
			if (isTarget[hex])
				return depth[hex];

			const H3AdjacentSquares& adjacent = combat.adjacentSquares[hex];
			const INT16 neighbours[6] = { adjacent.topRight, adjacent.right, adjacent.bottomRight,
				adjacent.bottomLeft, adjacent.left, adjacent.topLeft };
			for (INT16 next : neighbours)
			{
				if (next < 0 || next >= HEX_COUNT || depth[next] >= 0)
					continue;
				depth[next] = depth[hex] + 1;
				queue[tail++] = next;
			}
		}
		return FAR_AWAY;
	}

	INT32 FrontGap(H3CombatManager& combat, INT32 side)
	{
		INT32 gap = FAR_AWAY;
		for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
		{
			H3CombatMonster& ours = combat.stacks[side][i];
			if (!IsFighter(ours))
				continue;
			for (INT32 j = 0; j < STACKS_PER_SIDE; ++j)
			{
				H3CombatMonster& theirs = combat.stacks[1 - side][j];
				if (!IsFighter(theirs))
					continue;
				const INT32 distance = DistanceIgnoringObstacles(combat, theirs, ours);
				if (distance < gap)
					gap = distance;
			}
		}
		return gap;
	}

	struct LastDecision
	{
		const H3CombatMonster* stack = nullptr;
		INT32 round = -1;
		BOOL waiting = FALSE;
		BOOL morale = FALSE;
		planner::Kind kind = planner::Kind::Defend;
	};

	struct BattleState
	{
		const void* key[3] = {};
		INT32 round = -1;
		INT32 gapAtRoundStart = FAR_AWAY;
		INT32 stalledRounds = 0;
		LastDecision last;
		bool resultLogged = false;
	};

	BattleState g_battle;
	bool g_probeStockAI = false;

	void LogArmies(H3CombatManager& combat)
	{
		for (INT32 side = 0; side < 2; ++side)
		{
			for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
			{
				H3CombatMonster& stack = combat.stacks[side][i];
				if (stack.type < 0 || stack.numberAtStart <= 0)
					continue;
				Log("  side %d %s %d/%d at %d speed %d morale %d%s", side, stack.GetCreatureName(), stack.numberAlive,
					stack.numberAtStart, stack.position, IsAlive(stack) ? stack.GetStackSpeed() : 0, stack.spellsData.morale,
					stack.info.flags.shooter ? " shooter" : "");
			}
		}
	}

	void TrackRound(H3CombatManager& combat, INT32 side)
	{
		const void* key[3] = { combat.army[0], combat.army[1], combat.mapitem };
		if (memcmp(key, g_battle.key, sizeof(key)) != 0 || combat.turn < g_battle.round)
		{
			g_battle = BattleState();
			memcpy(g_battle.key, key, sizeof(key));
			Log("battle start, difficulty %d, auto %d, not AI %d/%d, human %d/%d", H3Internal::_main()->playersInfo.difficulty,
				combat.autoCombat, combat.isNotAI[0], combat.isNotAI[1], combat.isHuman[0], combat.isHuman[1]);
		}
		if (combat.turn == g_battle.round)
			return;

		const INT32 gap = FrontGap(combat, side);
		if (g_battle.round >= 0)
			g_battle.stalledRounds = gap >= g_battle.gapAtRoundStart ? g_battle.stalledRounds + 1 : 0;
		g_battle.gapAtRoundStart = gap;
		g_battle.round = combat.turn;
		Log("round %d: gap %d, stalled rounds %d", combat.turn, gap, g_battle.stalledRounds);
		LogArmies(combat);
	}

	bool IsRepeatedRequest(const H3CombatManager& combat, const H3CombatMonster& stack)
	{
		return g_battle.last.stack == &stack && g_battle.last.round == combat.turn && g_battle.last.waiting == (combat.waitPhase != 0)
			&& g_battle.last.morale == stack.info.flags.morale && g_battle.last.kind != planner::Kind::Wait;
	}

	LPCSTR ActionName(INT32 action)
	{
		static const LPCSTR names[] = { "cancel", "cast", "walk", "defend", "retreat", "surrender", "attack", "shoot",
			"wait", "catapult", "creature spell", "first aid", "nothing" };
		return action >= 0 && action < static_cast<INT32>(sizeof(names) / sizeof(names[0])) ? names[action] : "unknown";
	}

	struct StackRecord
	{
		INT32 count;
		INT32 position;
	};

	void RecordStacks(H3CombatManager& combat, StackRecord (&records)[2][STACKS_PER_SIDE])
	{
		for (INT32 side = 0; side < 2; ++side)
			for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
				records[side][i] = { combat.stacks[side][i].numberAlive, combat.stacks[side][i].position };
	}

	void LogChanges(H3CombatManager& combat, const StackRecord (&before)[2][STACKS_PER_SIDE])
	{
		for (INT32 side = 0; side < 2; ++side)
		{
			for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
			{
				H3CombatMonster& stack = combat.stacks[side][i];
				const StackRecord& old = before[side][i];
				if (stack.type < 0 || (old.count == stack.numberAlive && old.position == stack.position))
					continue;
				Log("      side %d %s x%d -> x%d, hex %d -> %d", side, stack.GetCreatureName(), old.count,
					stack.numberAlive, old.position, stack.position);
			}
		}
	}

	bool SideHasFighters(H3CombatManager& combat, INT32 side)
	{
		for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
			if (IsFighter(combat.stacks[side][i]))
				return true;
		return false;
	}

	void LogResultIfOver(H3CombatManager& combat)
	{
		if (g_battle.resultLogged || (SideHasFighters(combat, 0) && SideHasFighters(combat, 1)))
			return;
		g_battle.resultLogged = true;
		for (INT32 side = 0; side < 2; ++side)
		{
			INT32 lost = 0;
			CHAR detail[512] = {};
			size_t used = 0;
			for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
			{
				H3CombatMonster& stack = combat.stacks[side][i];
				if (stack.type < 0 || stack.numberAtStart <= 0 || stack.info.flags.siegeWeapon)
					continue;
				const INT32 dead = stack.numberAtStart - (stack.numberAlive > 0 ? stack.numberAlive : 0);
				lost += dead;
				if (dead > 0 && used + 48 < sizeof(detail))
					used += _snprintf_s(detail + used, sizeof(detail) - used, _TRUNCATE, " %s %d", stack.GetCreatureName(), dead);
			}
			Log("battle over: side %d lost %d%s", side, lost, detail);
		}
	}

	void RunStockPrologue(H3CombatManager& combat, H3CombatMonster& stack)
	{
		combat.activeStack = nullptr;
		THISCALL_2(VOID, CLEAR_HOVER_HIGHLIGHT, &combat, 1);
		*reinterpret_cast<INT32*>(reinterpret_cast<BYTE*>(&stack) + STACK_AI_FIELD_A) = -1;
		*reinterpret_cast<INT32*>(reinterpret_cast<BYTE*>(&stack) + STACK_AI_FIELD_B) = -1;
	}

	void ApplyDecision(H3CombatManager& combat, const planner::Decision& decision)
	{
		switch (decision.kind)
		{
		case planner::Kind::Wait:
			combat.action = NH3Combat::BA_WAIT;
			break;
		case planner::Kind::Walk:
			combat.action = NH3Combat::BA_WALK;
			combat.actionTarget = decision.hex;
			break;
		case planner::Kind::Attack:
			combat.action = NH3Combat::BA_WALK_ATTACK;
			combat.actionParameter = decision.hex;
			combat.actionTarget = decision.target->position;
			break;
		case planner::Kind::Shoot:
			combat.action = NH3Combat::BA_SHOOT;
			combat.actionTarget = decision.target->position;
			break;
		default:
			combat.action = NH3Combat::BA_DEFEND;
			break;
		}
	}
}

void AutoCombatAIPlugin::ChooseCreatureAction(HiHook* h, H3CombatManager* combat, INT32 side)
{
	if (!combat->isHuman[side] || combat->tacticsPhase || (combat->town && side != 0))
	{
		g_probeStockAI = !combat->isHuman[side] && !combat->tacticsPhase;
		if (g_probeStockAI)
		{
			TrackRound(*combat, side);
			H3CombatMonster& actor = combat->stacks[combat->currentMonSide][combat->currentMonIndex];
			Log("    stock turn round %d %s x%d at %d", combat->turn, actor.GetCreatureName(), actor.numberAlive, actor.position);			static CHAR prediction[4096];
			GameSideEffects sideEffects(*combat);
			planner::PredictStockTurn(*combat, actor, prediction, sizeof(prediction));
			sideEffects.Restore();
			Log("    port prediction:%s", prediction);
		}
		THISCALL_2(VOID, h->GetDefaultFunc(), combat, side);
		g_probeStockAI = false;
		return;
	}

	H3CombatMonster& stack = combat->stacks[combat->currentMonSide][combat->currentMonIndex];
	if (!IsFighter(stack) || stack.side != side)
	{
		THISCALL_2(VOID, h->GetDefaultFunc(), combat, side);
		return;
	}

	RunStockPrologue(*combat, stack);
	TrackRound(*combat, side);
	planner::Context context;
	context.stalledRounds = g_battle.stalledRounds;
	planner::Decision decision;
	const DWORD started = GetTickCount();
	if (IsRepeatedRequest(*combat, stack))
		Log("round %d %s asked again after %s; defending", combat->turn, stack.GetCreatureName(), planner::KindName(g_battle.last.kind));
	else
	{
		GameSideEffects sideEffects(*combat);
		decision = planner::Choose(*combat, side, stack, context);
		sideEffects.Restore();
	}
	const DWORD elapsed = GetTickCount() - started;
	ApplyDecision(*combat, decision);
	g_battle.last.stack = &stack;
	g_battle.last.round = combat->turn;
	g_battle.last.waiting = combat->waitPhase != 0;
	g_battle.last.morale = stack.info.flags.morale;
	g_battle.last.kind = decision.kind;

	Log("round %d %s x%d at %d: %s hex %d target %s at %d | gain %.0f loss %.0f score %.1f | %lu ms | %s", combat->turn,
		stack.GetCreatureName(), stack.numberAlive, stack.position, planner::KindName(decision.kind), decision.hex,
		decision.target ? decision.target->GetCreatureName() : "-", decision.target ? decision.target->position : -1,
		decision.gained, decision.lost, decision.score, elapsed, decision.alternatives);
	if (decision.trace[0])
		Log("    predicted:%s", decision.trace);
}

INT32 AutoCombatAIPlugin::ExecuteAction(HiHook* h, H3CombatManager* combat, INT32 a1, INT32 a2)
{
	if (combat->currentMonSide < 0 || combat->currentMonSide > 1 || combat->currentMonIndex < 0
		|| combat->currentMonIndex >= STACKS_PER_SIDE)
		return THISCALL_3(INT32, h->GetDefaultFunc(), combat, a1, a2);

	TrackRound(*combat, combat->currentActiveSide);
	const INT32 actorSide = combat->currentMonSide;
	const INT32 actorIndex = combat->currentMonIndex;
	H3CombatMonster& actor = combat->stacks[actorSide][actorIndex];
	const INT32 action = combat->action;
	const INT32 parameter = combat->actionParameter;
	const INT32 target = combat->actionTarget;
	const INT32 round = combat->turn;
	StackRecord before[2][STACKS_PER_SIDE];
	RecordStacks(*combat, before);
	const INT32 gate = combat->siegeKind;

	const INT32 result = THISCALL_3(INT32, h->GetDefaultFunc(), combat, a1, a2);

	Log("    act round %d side %d %s x%d at %d: %s param %d target %d", round, actorSide, actor.GetCreatureName(),
		before[actorSide][actorIndex].count, before[actorSide][actorIndex].position, ActionName(action), parameter, target);
	LogChanges(*combat, before);
	if (combat->town && combat->siegeKind != gate)
		Log("      gate %d -> %d", gate, combat->siegeKind);
	if (action == NH3Combat::BA_CATAPULT && combat->town)
		Log("      fort hp upper tower %d, walls %d %d, gate %d, walls %d %d, lower tower %d, keep %d", combat->fortWallsHp[5],
			combat->fortWallsHp[6], combat->fortWallsHp[8], combat->fortWallsHp[9], combat->fortWallsHp[10], combat->fortWallsHp[12],
			combat->fortWallsHp[13], combat->fortWallsHp[14]);
	LogResultIfOver(*combat);
	return result;
}

void __stdcall _HH_ChooseCreatureAction(HiHook* h, H3CombatManager* combat, INT32 side)
{
	AutoCombatAI.ChooseCreatureAction(h, combat, side);
}

INT32 __stdcall _HH_ExecuteAction(HiHook* h, H3CombatManager* combat, INT32 a1, INT32 a2)
{
	return AutoCombatAI.ExecuteAction(h, combat, a1, a2);
}

CHAR __stdcall _HH_StockScoreStandHex(HiHook* h, INT32* evaluator)
{
	const CHAR found = THISCALL_1(CHAR, h->GetDefaultFunc(), evaluator);
	if (g_probeStockAI)
	{
		H3CombatMonster* target = reinterpret_cast<H3CombatMonster*>(evaluator[EVALUATOR_TARGET]);
		Log("      stock target %s x%d at %d: stand %d turns %d hex value %d", target->GetCreatureName(), target->numberAlive,
			target->position, evaluator[EVALUATOR_STAND], evaluator[EVALUATOR_TURNS], evaluator[EVALUATOR_HEX_VALUE]);
	}
	return found;
}

INT32 __stdcall _HH_StockAttackValue(HiHook* h, void* context, H3CombatMonster* attacker, H3CombatMonster* target, INT32 ranged, INT32 joust)
{
	const INT32 value = THISCALL_5(INT32, h->GetDefaultFunc(), context, attacker, target, ranged, joust);
	if (g_probeStockAI)
		Log("      stock attack value %s x%d at %d on %s x%d at %d, joust %d: %d (from 0x%X)", attacker->GetCreatureName(),
			attacker->numberAlive, attacker->position, target->GetCreatureName(), target->numberAlive, target->position, joust,
			value, h->GetReturnAddress());
	return value;
}

INT32 __stdcall _HH_StockRandomInRange(HiHook* h, INT32 low, INT32 high)
{
	const INT32 roll = FASTCALL_2(INT32, h->GetDefaultFunc(), low, high);
	if (g_probeStockAI)
		Log("      stock roll %d..%d: %d", low, high, roll);
	return roll;
}

int __stdcall _LH_StockTargetRoll(LoHook* h, HookContext* c)
{
	if (g_probeStockAI)
	{
		H3CombatMonster* target = *reinterpret_cast<H3CombatMonster**>(c->ebp + STOCK_ROLL_TARGET_FRAME_OFFSET);
		Log("      stock roll on %s x%d at %d: score %d x %d%%", target->GetCreatureName(), target->numberAlive, target->position,
			c->ebx, c->eax);
	}
	return EXEC_DEFAULT;
}

VOID __stdcall _HH_StockDangerMap(HiHook* h, H3CombatManager* combat, H3CombatMonster* actor, INT32* map, UINT32* threatened, void* context)
{
	THISCALL_5(VOID, h->GetDefaultFunc(), combat, actor, map, threatened, context);
	if (!g_probeStockAI)
		return;
	CHAR line[4096] = {};
	size_t used = 0;
	for (INT32 hex = 0; hex < HEX_COUNT && used + 24 < sizeof(line); ++hex)
		if (map[hex] != 0)
			used += _snprintf_s(line + used, sizeof(line) - used, _TRUNCATE, " %d:%d", hex, map[hex]);
	Log("      stock danger map, threatened mask 0x%X:%s", *threatened, line);
}

void AutoCombatAIPlugin::Start()
{
	if (memcmp(reinterpret_cast<const void*>(AI_CHOOSE_CREATURE_ACTION), kStockChooseActionStart, sizeof(kStockChooseActionStart)) != 0)
	{
		Log("0x422370 does not hold the stock bytes; hook not installed");
		return;
	}
	Hook(AI_CHOOSE_CREATURE_ACTION, Splice, Thiscall, ::_HH_ChooseCreatureAction);
	Hook(EXECUTE_ACTION, Splice, Thiscall, ::_HH_ExecuteAction);
	Log("hooks installed at 0x422370 and 0x4786B0");

	struct Probe
	{
		DWORD address;
		BYTE start[6];
		HConvention callType;
		void* function;
	};
	const Probe probes[] = {
		{ STOCK_SCORE_STAND_HEX, { 0x57, 0x8B, 0xF9, 0x6A, 0x06, 0x6A }, Thiscall, reinterpret_cast<void*>(::_HH_StockScoreStandHex) },
		{ STOCK_ATTACK_VALUE, { 0x55, 0x8B, 0xEC, 0x53, 0x56, 0x57 }, Thiscall, reinterpret_cast<void*>(::_HH_StockAttackValue) },
		{ STOCK_RANDOM_IN_RANGE, { 0x56, 0x57, 0x8B, 0xF2, 0x8B, 0xF9 }, Fastcall, reinterpret_cast<void*>(::_HH_StockRandomInRange) },
		{ STOCK_DANGER_MAP, { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C }, Thiscall, reinterpret_cast<void*>(::_HH_StockDangerMap) },
	};
	for (const Probe& probe : probes)
	{
		if (memcmp(reinterpret_cast<const void*>(probe.address), probe.start, sizeof(probe.start)) != 0)
		{
			Log("stock AI probe at 0x%X skipped; bytes differ", probe.address);
			continue;
		}
		Hook(probe.address, Splice, probe.callType, probe.function);
		Log("stock AI probe installed at 0x%X", probe.address);
	}
	if (memcmp(reinterpret_cast<const void*>(STOCK_AFTER_TARGET_ROLL), kStockAfterTargetRoll, sizeof(kStockAfterTargetRoll)) == 0)
	{
		Hook(STOCK_AFTER_TARGET_ROLL, ::_LH_StockTargetRoll);
		Log("stock AI probe installed at 0x%X", STOCK_AFTER_TARGET_ROLL);
	}
	else
		Log("stock AI probe at 0x%X skipped; bytes differ", STOCK_AFTER_TARGET_ROLL);
}
