#include "Planner.hpp"

#include <cstdarg>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <float.h>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using namespace h3;

namespace planner
{
	namespace
	{
		constexpr DWORD PATHFINDER = 0x6992D4;
		constexpr DWORD FIND_PATHS = 0x4B2B00;
		constexpr DWORD PREVIEW_DAMAGE = 0x443C60;
		constexpr DWORD FIXED_BASE_DAMAGE = 0x442E80;
		constexpr DWORD CAN_SHOOT = 0x442610;
		constexpr INT32 PATH_NODES = 0x24;
		constexpr INT32 PATH_NODE_SIZE = 0x1E;
		constexpr INT32 PATH_NODE_FLAGS = 0x04;
		constexpr INT32 PATH_NODE_DISTANCE = 0x18;
		constexpr INT32 FULL_MAP_DISTANCE = 99;

		constexpr INT32 MAX_UNITS = STACKS_PER_SIDE * 2;
		constexpr INT32 FAR_AWAY = 1000;
		constexpr INT32 ROLLOUT_ROUNDS = 6;
		constexpr INT32 MAX_ROLLOUT_ACTIONS = 200;
		constexpr double LOSS_WEIGHT = 3.0;
		constexpr double DEFEND_DAMAGE_FACTOR = 0.85;
		constexpr double WAIT_BONUS = 0.01;
		constexpr double MOVE_COST_PER_HEX = 0.001;
		constexpr double APPROACH_BONUS_PER_HEX = 0.02;
		constexpr double SHOOTER_APPROACH_PER_HEX = 0.01;
		constexpr double DAMAGE_TIEBREAK = 0.001;
		constexpr double EXPOSURE_WEIGHT = 0.5;
		constexpr DWORD STOCK_AI_CONTEXT = 0x435B10;
		constexpr DWORD STOCK_FIGHT_VALUE = 0x442770;
		constexpr DWORD STOCK_SHOT_DAMAGE_FACTOR = 0x443AB0;
		constexpr DWORD STOCK_ESTIMATE_DAMAGE = 0x4424A0;
		constexpr double DESPERATE_CREATURE_VALUE = 1000.0;
		constexpr INT32 STOCK_ROLL_LOW = 75;
		constexpr INT32 STOCK_ROLL_HIGH = 100;
		constexpr INT32 REFINE_CANDIDATES = 4;
		constexpr INT64 REFINE_WORK_BUDGET = 90000;
		constexpr double REFINE_TIE_TOLERANCE = 1.0;
		constexpr INT32 MAX_CANDIDATES = 1024;
		constexpr INT32 BATTLE_COLUMNS = 17;
		constexpr INT32 MAX_GUARD_PLANS = 256;
		constexpr INT32 MORALE_CAP = 3;
		constexpr double MORALE_CHANCE_PER_POINT = 1.0 / 24.0;
		constexpr DWORD STOCK_STRONGER_AT_RANGE = 0x420C00;
		constexpr DWORD STOCK_AREA_THREAT = 0x41FAA0;
		constexpr INT32 STOCK_FULL_DISTANCE = 0x7F;
		constexpr INT32 STOCK_IMMOBILE_TURNS = 100;
		constexpr INT32 STOCK_SHOT_TURN_LIMIT = 6;
		constexpr INT32 STOCK_SHOT_FAR_DIVISOR = 5;
		constexpr INT32 STOCK_SHOT_DISABLED_DIVISOR = 10;
		constexpr INT32 STOCK_ROLL_SAMPLES = 64;
		constexpr DWORD STOCK_ROLL_SEED = 0x2545F491;
		constexpr UINT8 SQUARE_OBSTACLE = 2;
		constexpr INT32 DIRECTIONS = 6;
		constexpr INT32 WIDE_DIRECTIONS = 8;
		constexpr INT32 ROBUST_REPLIES = 3;
		constexpr INT32 THREAT_CACHE_SIZE = 4096;
		constexpr INT32 PATH_CACHE_SIZE = 2048;
		constexpr INT32 SHOT_HALF_RANGE = 10;
		constexpr INT32 SIEGE_ROWS = 11;
		constexpr INT32 WALL_HEX[SIEGE_ROWS] = { 12, 29, 45, 62, 78, 96, 112, 130, 147, 165, 182 };
		constexpr INT32 GATE_HEX = 96;
		constexpr INT32 GATE_MOAT_HEX = 95;
		constexpr INT32 GATE_INNER_HEX = 94;
		constexpr INT32 GATE_CLOSED = 3;
		constexpr INT32 GATE_DESTROYED = 0;
		constexpr INT32 FORTRESS_TOWN = 7;
		constexpr INT32 DEFENDER_SIDE = 1;
		constexpr INT32 FORT_ELEMENTS = 18;
		constexpr INT32 TOWERS = 3;
		constexpr INT32 TOWER_ESTIMATE_PER_CREATURE = 3;
		constexpr INT32 TOWER_MAX_PER_CREATURE = 4;
		constexpr INT32 STAY_INSIDE_ELEMENTS[] = { 6, 8, 9, 10, 12 };
		constexpr DWORD CATAPULT_TARGETS = 0x63BE60;
		constexpr INT32 CATAPULT_TARGET_SIZE = 0xC;
		constexpr INT32 CATAPULT_TARGET_ROW = 2;
		constexpr INT32 CATAPULT_TARGET_ELEMENT = 8;
		constexpr INT32 CATAPULT_TARGET_COUNT = 8;
		constexpr INT32 BREACHES = 4;
		constexpr INT32 CATAPULT_WALLS[BREACHES] = { 1, 2, 4, 5 };
		constexpr INT32 CATAPULT_MISS_WALLS[BREACHES] = { 1, 2, 5, 4 };
		constexpr INT32 CATAPULT_FALLBACK[] = { 3, 7, 0, 6 };
		constexpr INT32 CATAPULT_UPPER_TOWER = 0;
		constexpr INT32 CATAPULT_GATE = 3;
		constexpr INT32 CATAPULT_LOWER_TOWER = 6;
		constexpr INT32 CATAPULT_KEEP = 7;
		constexpr INT32 FORT_CASTLE = 3;
		constexpr INT32 FORT_CITADEL = 2;
		constexpr INT32 KEEP_HEX = 254;
		constexpr INT32 UPPER_TOWER_HEX = 255;
		constexpr INT32 LOWER_TOWER_HEX = 251;
		constexpr DWORD BALLISTICS_TABLE = 0x679C84;
		constexpr INT32 BALLISTICS_ROW_SIZE = 8;
		constexpr INT32 BALLISTICS_SHOTS = 4;
		constexpr INT32 BALLISTICS_NO_DAMAGE = 5;
		constexpr INT32 BALLISTICS_ONE_POINT = 6;
		constexpr INT32 HERO_BALLISTICS = 0xD3;
		constexpr INT32 FORT_HP_SCALE = 1000;
		constexpr DWORD WALL_HEX_TABLE = 0x63BD00;
		constexpr DWORD STOCK_SIEGE_BLOCKED = 0x4695F0;
		constexpr DWORD MOAT_FIRST_LINE = 0x63BCE8;
		constexpr DWORD MOAT_SECOND_LINE = 0x63BCF4;
		constexpr DWORD MOAT_DAMAGE_TABLE = 0x63BD18;
		constexpr INT32 SOD_MAP_KIND = 2;
		constexpr INT32 MOAT_DEFENCE = 3;
		constexpr INT32 MOAT_AVOID_ALWAYS = 4;
		constexpr INT32 MOAT_AVOID_NEVER = 40;
		constexpr DWORD STOCK_ATTACK = 0x442130;
		constexpr DWORD STOCK_DEFENCE = 0x4422B0;
		constexpr double ATTACK_BONUS_PER_POINT = 0.05;
		constexpr INT32 ATTACK_BONUS_CAP = 60;
		constexpr double DEFENCE_CUT_PER_POINT = 0.025;
		constexpr double DEFENCE_CUT_FLOOR = 0.3;
		constexpr double ROBUST_WEIGHTS[ROBUST_REPLIES] = { 0.0, 0.0, 0.0 };

		struct Unit
		{
			H3CombatMonster* stack;
			INT32 type;
			INT32 side;
			INT32 sideIndex;
			INT32 maxHp;
			INT32 speed;
			INT32 tailOffset;
			INT32 facing;
			INT32 maxRetaliations;
			INT32 morale;
			double value;
			double fightValue;
			double fvPerCreature;
			double fvRanged;
			double shotDamageFactor;
			bool wide;
			bool flyer;
			bool teleporter;
			bool shooter;
			bool noRetaliation;
			bool doubleAttack;
			bool strikesTwice;
			bool breath;
			bool allAround;
			bool threeHeaded;
			bool deathCloud;
			bool cloudShooter;
			bool alive;
			bool undead;
			bool summoned;
			bool harpy;
			bool disabled;
			bool petrified;
			bool threatens;
			bool fireball;
			bool noMeleePenalty;
			bool heroBlast;
			bool rangePenalty;
			bool wallExempt;
			bool moatWalker;
			bool moatAtStart;
		};

		struct UnitState
		{
			INT32 count;
			INT32 topHp;
			INT32 retaliations;
			INT32 head;
			bool done;
			bool waited;
			bool defending;
		};

		struct State
		{
			UnitState u[MAX_UNITS];
			INT32 lastSide;
			INT32 round;
			bool waitPhase;
			INT32 fort[CATAPULT_TARGET_COUNT];
		};

		struct Occupancy
		{
			INT32 at[HEX_COUNT];
			UINT8 breached;
			bool gateOpen;
			INT32& operator[](INT32 hex) { return at[hex]; }
			INT32 operator[](INT32 hex) const { return at[hex]; }
		};

		struct Outcome
		{
			double gained = 0.0;
			double lost = 0.0;
		};

		struct Candidate
		{
			Kind kind;
			INT32 hex;
			INT32 target;
			INT32 guard = -1;
		};

		struct GuardPlan
		{
			bool guarded[MAX_UNITS];
			bool mover[MAX_UNITS];
		};

		H3CombatManager* g_combat;
		thread_local PCHAR g_trace = nullptr;
		thread_local size_t g_traceSize = 0;
		Unit g_units[MAX_UNITS];
		INT32 g_unitCount;
		INT32 g_actor;
		INT32 g_side;
		INT32 g_startRound;
		bool g_stockCareful;
		bool g_strongerAtRange;
		bool g_obstacle[HEX_COUNT];
		bool g_deadStackHex[2][HEX_COUNT];
		bool g_stockOpen[HEX_COUNT];
		INT16 g_adjacent[HEX_COUNT][6];
		bool g_refineActive = false;
		thread_local bool g_searching = false;
		INT32 g_refineUntilRound = 0;
		bool g_enemyCanWait;
		thread_local bool g_inMoraleAction = false;
		State g_start;
		bool g_standable[HEX_COUNT];
		bool g_headOk[HEX_COUNT];
		INT32 g_actorDistance[HEX_COUNT];
		double g_avgMelee[MAX_UNITS][MAX_UNITS];
		double g_maxMelee[MAX_UNITS][MAX_UNITS];
		double g_minMelee[MAX_UNITS][MAX_UNITS];
		double g_avgRanged[MAX_UNITS][MAX_UNITS];
		double g_maxRanged[MAX_UNITS][MAX_UNITS];
		std::vector<INT32> g_stockDamage[MAX_UNITS][MAX_UNITS];
		std::vector<INT32> g_stockRangedDamage[MAX_UNITS][MAX_UNITS];
		GuardPlan g_guardPlans[MAX_GUARD_PLANS];
		INT32 g_guardPlanCount;
		thread_local const GuardPlan* g_guard = nullptr;
		bool g_robust = false;
		INT32 g_cacheGeneration = 0;
		thread_local INT64 g_work = 0;
		thread_local INT32 g_task = -1;
		bool g_siege = false;
		INT32 g_fortLevel = 0;
		INT32 g_gate = 0;
		bool g_gateCorpse = false;
		bool g_gateHeld = false;
		bool g_gateInnerHolds = false;
		bool g_wallBlocked[SIEGE_ROWS];
		INT32 g_wallHp[FORT_ELEMENTS];
		INT32 g_towerCount[TOWERS];
		INT32 g_towerTarget[TOWERS];
		INT32 g_towers = 0;
		bool g_stayInside = false;
		bool g_stayHexBlocked[sizeof(STAY_INSIDE_ELEMENTS) / sizeof(STAY_INSIDE_ELEMENTS[0])];
		INT32 g_stayTarget[sizeof(STAY_INSIDE_ELEMENTS) / sizeof(STAY_INSIDE_ELEMENTS[0])];
		bool g_catapult = false;
		INT32 g_ballistics = 0;
		INT8 g_ballisticsRow[BALLISTICS_ROW_SIZE];
		INT32 g_breachHex[BREACHES];
		INT8 g_breachSlot[HEX_COUNT];
		bool g_moat = false;
		bool g_moatHex[HEX_COUNT];
		INT32 g_moatDamage = 0;
		INT32 g_moatTable = 0;
		double g_moatMelee[MAX_UNITS][MAX_UNITS];
		double g_moatRanged[MAX_UNITS][MAX_UNITS];

		INT32 TaskStockDamage(INT32 a, INT32 t, INT32 count, bool ranged);
		void ScoreInPool(const State& state, INT32 a, const Candidate* candidates, INT32 count, double* scores);

		bool IsFighter(const H3CombatMonster& stack)
		{
			return stack.type >= 0 && stack.numberAlive > 0 && !stack.info.flags.siegeWeapon;
		}

		bool Ours(INT32 u)
		{
			return g_units[u].side == g_side;
		}

		void Neighbours(INT32 hex, INT16 (&out)[6])
		{
			memcpy(out, g_adjacent[hex], sizeof(out));
		}

		INT32 HexesAt(INT32 u, INT32 head, INT32 (&hexes)[2])
		{
			INT32 count = 0;
			if (head >= 0 && head < HEX_COUNT)
				hexes[count++] = head;
			if (g_units[u].wide)
			{
				const INT32 tail = head + g_units[u].tailOffset;
				if (tail >= 0 && tail < HEX_COUNT)
					hexes[count++] = tail;
			}
			return count;
		}

		bool Adjacent(INT32 a, INT32 headA, INT32 b, INT32 headB)
		{
			INT32 from[2];
			const INT32 fromCount = HexesAt(a, headA, from);
			INT32 to[2];
			const INT32 toCount = HexesAt(b, headB, to);
			for (INT32 i = 0; i < fromCount; ++i)
			{
				INT16 next[6];
				Neighbours(from[i], next);
				for (INT16 n : next)
					for (INT32 j = 0; j < toCount; ++j)
						if (n == to[j])
							return true;
			}
			return false;
		}

		void AdjacencyMask(INT32 t, INT32 head, bool (&mask)[HEX_COUNT])
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				mask[hex] = false;
			INT32 hexes[2];
			const INT32 count = HexesAt(t, head, hexes);
			for (INT32 i = 0; i < count; ++i)
			{
				INT16 next[6];
				Neighbours(hexes[i], next);
				for (INT16 n : next)
					if (n >= 0 && n < HEX_COUNT)
						mask[n] = true;
			}
		}

		bool Touches(INT32 u, INT32 head, const bool (&mask)[HEX_COUNT])
		{
			INT32 hexes[2];
			const INT32 count = HexesAt(u, head, hexes);
			for (INT32 i = 0; i < count; ++i)
				if (mask[hexes[i]])
					return true;
			return false;
		}

		UINT8 BreachBits(const State& state)
		{
			UINT8 bits = 0;
			for (INT32 k = 0; g_siege && k < BREACHES; ++k)
				if (state.fort[CATAPULT_WALLS[k]] <= 0)
					bits |= 1u << k;
			return bits;
		}

		bool IsGateHex(INT32 hex)
		{
			return hex == GATE_HEX || hex == GATE_MOAT_HEX;
		}

		bool GateOpen(const State& state)
		{
			if (!g_siege)
				return false;
			if (g_gate == GATE_DESTROYED || state.fort[CATAPULT_GATE] <= 0 || g_gateHeld)
				return true;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				INT32 hexes[2];
				const INT32 count = HexesAt(u, state.u[u].head, hexes);
				for (INT32 i = 0; i < count; ++i)
					if (IsGateHex(hexes[i]) || (g_gateInnerHolds && hexes[i] == GATE_INNER_HEX && g_units[u].side == DEFENDER_SIDE))
						return true;
			}
			return false;
		}

		bool Breached(INT32 hex, UINT8 bits)
		{
			return g_breachSlot[hex] >= 0 && ((bits >> g_breachSlot[hex]) & 1) != 0;
		}

		void BuildOccupancy(const State& state, Occupancy& occupant)
		{
			occupant.breached = BreachBits(state);
			occupant.gateOpen = GateOpen(state);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				occupant[hex] = -1;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				if (state.u[u].count <= 0)
					continue;
				INT32 hexes[2];
				const INT32 count = HexesAt(u, state.u[u].head, hexes);
				for (INT32 i = 0; i < count; ++i)
					occupant[hexes[i]] = u;
			}
		}

		INT32 Adj(INT32 hex, INT32 dir)
		{
			if (hex < 0 || hex >= HEX_COUNT || dir < 0 || dir >= DIRECTIONS)
				return -1;
			return g_adjacent[hex][dir];
		}

		bool FacingRight(INT32 u)
		{
			return g_units[u].tailOffset > 0;
		}

		INT32 TailOf(INT32 u, INT32 head)
		{
			return g_units[u].wide ? head + g_units[u].tailOffset : head;
		}

		INT32 StepFrom(INT32 u, INT32 hex, INT32 dir)
		{
			if (dir == 6)
				dir = g_units[u].facing == 1 ? 5 : 0;
			else if (dir == 7)
				dir = g_units[u].facing == 1 ? 3 : 2;
			return Adj(hex, dir);
		}

		INT32 Around(INT32 u, INT32 head, INT32 dir)
		{
			INT32 hex = head;
			if (g_units[u].wide)
			{
				if (!FacingRight(u))
				{
					if (dir > 2)
						--hex;
				}
				else if (dir < 3 || dir > 5)
					++hex;
			}
			if (hex < 0 || hex >= HEX_COUNT)
				return -1;
			return StepFrom(u, hex, dir);
		}

		INT32 DirectionCount(INT32 u)
		{
			return g_units[u].wide ? WIDE_DIRECTIONS : DIRECTIONS;
		}

		INT32 StockFacingDirection(INT32 a, INT32 aHead, INT32 t, INT32 tHead)
		{
			const INT32 tTail = TailOf(t, tHead);
			for (INT32 dir = 0; dir < DirectionCount(a); ++dir)
			{
				const INT32 hex = Around(a, aHead, dir);
				if (hex == tHead || hex == tTail)
					return dir;
			}
			return -1;
		}

		INT32 ExecDirection(INT32 a, INT32 aHead, INT32 t, INT32 tHead)
		{
			const INT32 tTail = TailOf(t, tHead);
			INT32 fallback = -1;
			for (INT32 dir = 0; dir < DirectionCount(a); ++dir)
			{
				const INT32 hex = Around(a, aHead, dir);
				if (hex < 0 || (hex != tHead && hex != tTail))
					continue;
				if (dir > 5 || FacingRight(a) != (dir > 2))
					return dir;
				if (fallback < 0)
					fallback = dir;
			}
			return fallback;
		}

		UINT32 StrikeMask(INT32 a, INT32 aHead, INT32 t, INT32 tHead)
		{
			static const INT32 ringPosition[WIDE_DIRECTIONS] = { 0, 1, 2, 4, 5, 6, 7, 3 };
			static const INT32 ringDirection[WIDE_DIRECTIONS] = { 0, 1, 2, 7, 3, 4, 5, 6 };
			const UINT32 all = g_units[a].wide ? 0xFF : 0x3F;
			if (!g_units[a].threeHeaded)
				return all;
			const INT32 dir = StockFacingDirection(a, aHead, t, tHead);
			if (dir < 0)
				return all;
			if (!g_units[a].wide)
				return (1u << dir) | (1u << ((dir + 5) % DIRECTIONS)) | (1u << ((dir + 1) % DIRECTIONS));
			const INT32 left = ringDirection[(ringPosition[dir] + 7) % WIDE_DIRECTIONS];
			const INT32 right = ringDirection[(ringPosition[dir] + 1) % WIDE_DIRECTIONS];
			return (1u << dir) | (1u << left) | (1u << right);
		}

		bool GateBlocked(INT32 activeSide, const Occupancy& occupant)
		{
			if (occupant.gateOpen)
				return false;
			return activeSide != DEFENDER_SIDE || g_gateCorpse || occupant[GATE_MOAT_HEX] >= 0 || occupant[GATE_INNER_HEX] >= 0;
		}

		bool Inside(INT32 hex)
		{
			return hex >= WALL_HEX[hex / BATTLE_COLUMNS];
		}

		bool WallLineBlocked(INT32 from, INT32 to, UINT8 breached, bool gateOpen)
		{
			const INT32 dc = to % BATTLE_COLUMNS - from % BATTLE_COLUMNS;
			const INT32 dr = to / BATTLE_COLUMNS - from / BATTLE_COLUMNS;
			if (dc == 0 && dr == 0)
				return false;
			const INT32 adc = dc < 0 ? -dc : dc;
			const INT32 adr = dr < 0 ? -dr : dr;
			float stepCol;
			float stepRow;
			INT32 n;
			if (adc > adr)
			{
				stepCol = dc > 0 ? 1.0f : -1.0f;
				stepRow = static_cast<float>(dr) / static_cast<float>(adc);
				n = adc;
			}
			else
			{
				stepRow = dr > 0 ? 1.0f : -1.0f;
				stepCol = static_cast<float>(dc) / static_cast<float>(adr);
				n = adr;
			}
			stepCol /= static_cast<float>(BATTLE_COLUMNS);
			stepRow /= static_cast<float>(BATTLE_COLUMNS);
			float col = static_cast<float>(from % BATTLE_COLUMNS);
			float row = static_cast<float>(from / BATTLE_COLUMNS);
			for (INT32 i = 0; i < n * BATTLE_COLUMNS; ++i)
			{
				col += stepCol;
				row += stepRow;
				const INT32 hex = static_cast<INT32>(row) * BATTLE_COLUMNS + static_cast<INT32>(col);
				if (hex == GATE_HEX)
				{
					if (!gateOpen)
						return true;
					continue;
				}
				for (INT32 k = 0; k < SIEGE_ROWS; ++k)
					if (hex == WALL_HEX[k] && g_wallBlocked[k] && !Breached(hex, breached))
						return true;
			}
			return false;
		}

		bool WallShot(const State& state, INT32 a, INT32 aHead, INT32 t, INT32 tHead)
		{
			if (!g_siege || g_units[a].wallExempt)
				return false;
			const INT32 from = TailOf(a, aHead);
			if (from < 0 || from >= HEX_COUNT || tHead < 0 || tHead >= HEX_COUNT)
				return false;
			return !Inside(from) && Inside(tHead) && WallLineBlocked(from, tHead, BreachBits(state), GateOpen(state));
		}

		bool MoatHexNow(INT32 hex, bool gateOpen)
		{
			return g_moatHex[hex] && !(gateOpen && (hex == GATE_MOAT_HEX || hex == GATE_INNER_HEX));
		}

		bool OnMoat(const State& state, INT32 u, INT32 head)
		{
			if (!g_moat)
				return false;
			const bool gateOpen = GateOpen(state);
			INT32 hexes[2];
			const INT32 count = HexesAt(u, head, hexes);
			for (INT32 i = 0; i < count; ++i)
				if (MoatHexNow(hexes[i], gateOpen))
					return true;
			return false;
		}

		bool MoatStop(const State& state, INT32 u, INT32 head)
		{
			return g_units[u].moatWalker && OnMoat(state, u, head);
		}

		bool IsFree(INT32 hex, const Occupancy& occupant, INT32 self)
		{
			if (hex < 0 || hex >= HEX_COUNT)
				return false;
			const bool open = g_siege && IsGateHex(hex) ? !GateBlocked(g_side, occupant) : g_standable[hex] || Breached(hex, occupant.breached);
			return open && (occupant[hex] < 0 || occupant[hex] == self);
		}

		bool HeadOk(INT32 hex, const Occupancy& occupant)
		{
			return g_headOk[hex] || Breached(hex, occupant.breached) || (occupant.gateOpen && IsGateHex(hex));
		}

		bool CanStand(INT32 u, INT32 head, const Occupancy& occupant)
		{
			INT32 hexes[2];
			const INT32 count = HexesAt(u, head, hexes);
			if (count < (g_units[u].wide ? 2 : 1) || !HeadOk(head, occupant))
				return false;
			for (INT32 i = 0; i < count; ++i)
				if (!IsFree(hexes[i], occupant, u))
					return false;
			return true;
		}

		void Reach(INT32 u, const State& state, const Occupancy& occupant, INT32 maxSteps, INT32 (&distance)[HEX_COUNT])
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				distance[hex] = -1;
			const INT32 start = state.u[u].head;
			if (start < 0 || start >= HEX_COUNT)
				return;
			INT32 queue[HEX_COUNT];
			INT32 head = 0;
			INT32 tail = 0;
			distance[start] = 0;
			queue[tail++] = start;
			while (head < tail)
			{
				const INT32 hex = queue[head++];
				if (distance[hex] >= maxSteps || (hex != start && MoatStop(state, u, hex)))
					continue;
				INT16 next[6];
				Neighbours(hex, next);
				for (INT16 n : next)
				{
					if (n < 0 || n >= HEX_COUNT || distance[n] >= 0)
						continue;
					if (!g_units[u].flyer && !CanStand(u, n, occupant))
						continue;
					distance[n] = distance[hex] + 1;
					queue[tail++] = n;
				}
			}
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				if (hex != start && distance[hex] >= 0 && !CanStand(u, hex, occupant))
					distance[hex] = -1;
		}

		bool HasAdjacentEnemy(const State& state, INT32 u)
		{
			bool mask[HEX_COUNT];
			AdjacencyMask(u, state.u[u].head, mask);
			for (INT32 e = 0; e < g_unitCount; ++e)
				if (g_units[e].side != g_units[u].side && state.u[e].count > 0 && Touches(e, state.u[e].head, mask))
					return true;
			return false;
		}

		INT32 ApplyDamage(State& state, INT32 u, double damage)
		{
			UnitState& s = state.u[u];
			if (s.count <= 0 || damage <= 0.0)
				return 0;
			INT32 dmg = static_cast<INT32>(damage);
			if (dmg < s.topHp)
			{
				s.topHp -= dmg;
				return 0;
			}
			dmg -= s.topHp;
			const INT32 maxHp = g_units[u].maxHp;
			const INT32 killed = 1 + dmg / maxHp;
			if (killed >= s.count)
			{
				const INT32 all = s.count;
				s.count = 0;
				s.topHp = 0;
				return all;
			}
			s.count -= killed;
			s.topHp = maxHp - dmg % maxHp;
			return killed;
		}

		double Scaled(double fullDamage, const State& state, INT32 attacker)
		{
			return fullDamage * state.u[attacker].count / g_units[attacker].stack->numberAlive;
		}

		double Received(double damage, const State& state, INT32 target)
		{
			return state.u[target].defending ? damage * DEFEND_DAMAGE_FACTOR : damage;
		}

		void Hit(State& state, INT32 target, double damage, Outcome& outcome)
		{
			const double value = ApplyDamage(state, target, damage) * g_units[target].value;
			if (Ours(target))
				outcome.lost += value;
			else
				outcome.gained += value;
		}

		double MoatDefence(const State& state, INT32 a, INT32 t, bool ranged)
		{
			if (!g_moat)
				return 1.0;
			const bool now = OnMoat(state, t, state.u[t].head);
			if (now == g_units[t].moatAtStart)
				return 1.0;
			const double factor = ranged ? g_moatRanged[a][t] : g_moatMelee[a][t];
			return now ? factor : 1.0 / factor;
		}

		void MoatHit(State& state, INT32 u, Outcome& outcome)
		{
			if (g_moatDamage > 0 && state.u[u].count > 0 && OnMoat(state, u, state.u[u].head))
				Hit(state, u, g_moatDamage, outcome);
		}

		void MoveTo(State& state, INT32 u, INT32 hex, Outcome& outcome)
		{
			const INT32 from = state.u[u].head;
			state.u[u].head = hex;
			if (hex != from)
				MoatHit(state, u, outcome);
		}

		double MeleeDamage(const State& state, INT32 a, INT32 t)
		{
			const double full = Ours(t) ? g_maxMelee[a][t] : g_avgMelee[a][t];
			return Received(Scaled(full, state, a), state, t) * MoatDefence(state, a, t, false);
		}

		INT32 HexDistance(INT32 a, INT32 b)
		{
			const INT32 aRow = a / BATTLE_COLUMNS;
			const INT32 bRow = b / BATTLE_COLUMNS;
			const INT32 shift = b % BATTLE_COLUMNS - a % BATTLE_COLUMNS;
			const INT32 x = (aRow + 1) / 2 - (bRow + 1) / 2 + shift;
			const INT32 y = bRow / 2 - aRow / 2 + shift;
			const INT32 ax = x < 0 ? -x : x;
			const INT32 ay = y < 0 ? -y : y;
			if ((x < 0) == (y < 0))
				return ax > ay ? ax : ay;
			return ax + ay;
		}

		bool FarShot(INT32 a, INT32 aHead, INT32 t, INT32 tHead)
		{
			if (!g_units[a].rangePenalty)
				return false;
			const INT32 from = TailOf(a, aHead);
			return HexDistance(from, tHead) > SHOT_HALF_RANGE && HexDistance(from, TailOf(t, tHead)) > SHOT_HALF_RANGE;
		}

		double RangedDamage(const State& state, INT32 a, INT32 t)
		{
			double full = Ours(t) ? g_maxRanged[a][t] : g_avgRanged[a][t];
			if (FarShot(a, state.u[a].head, t, state.u[t].head))
				full *= 0.5;
			if (WallShot(state, a, state.u[a].head, t, state.u[t].head))
				full *= 0.5;
			return Received(Scaled(full, state, a), state, t) * MoatDefence(state, a, t, true);
		}

		INT32 MeleeVictims(const State& state, INT32 a, INT32 t, INT32 (&victims)[MAX_UNITS])
		{
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			const INT32 aHead = state.u[a].head;
			const INT32 tHead = state.u[t].head;
			INT32 count = 0;
			auto add = [&](INT32 x)
			{
				for (INT32 i = 0; i < count; ++i)
					if (victims[i] == x)
						return;
				victims[count++] = x;
			};
			if (g_units[a].allAround)
			{
				const UINT32 mask = StrikeMask(a, aHead, t, tHead);
				for (INT32 dir = 0; dir < WIDE_DIRECTIONS; ++dir)
				{
					if (!(mask & (1u << dir)))
						continue;
					const INT32 hex = Around(a, aHead, dir);
					if (hex < 0 || hex >= HEX_COUNT)
						continue;
					const INT32 x = occupant[hex];
					if (x >= 0 && g_units[x].side != g_units[a].side)
						add(x);
				}
				add(t);
				return count;
			}
			add(t);
			if (g_units[a].breath)
			{
				const INT32 dir = ExecDirection(a, aHead, t, tHead);
				const INT32 beyond = dir >= 0 ? StepFrom(a, Around(a, aHead, dir), dir) : -1;
				if (beyond >= 0 && beyond < HEX_COUNT && occupant[beyond] >= 0 && occupant[beyond] != a)
					add(occupant[beyond]);
			}
			return count;
		}

		void Strike(State& state, INT32 a, INT32 t, const State& scaleFrom, Outcome& outcome)
		{
			INT32 victims[MAX_UNITS];
			const INT32 count = MeleeVictims(state, a, t, victims);
			double damage[MAX_UNITS];
			for (INT32 i = 0; i < count; ++i)
			{
				const INT32 x = victims[i];
				damage[i] = Received(Scaled(Ours(x) ? g_maxMelee[a][x] : g_avgMelee[a][x], scaleFrom, a), state, x) * MoatDefence(state, a, x, false);
			}
			for (INT32 i = 0; i < count; ++i)
				Hit(state, victims[i], damage[i], outcome);
		}

		void Exchange(State& state, INT32 a, INT32 t, Outcome& outcome)
		{
			State lowRoll = state;
			if (Ours(a))
				ApplyDamage(lowRoll, t, Received(Scaled(g_minMelee[a][t], state, a), state, t) * MoatDefence(state, a, t, false));
			Strike(state, a, t, state, outcome);
			if (!Ours(a))
				lowRoll = state;
			if (lowRoll.u[t].count > 0 && state.u[t].count > 0 && state.u[t].retaliations > 0 && !g_units[a].noRetaliation && !g_units[t].petrified)
			{
				--state.u[t].retaliations;
				if (state.u[a].count > 0)
					Strike(state, t, a, lowRoll, outcome);
			}
			if (g_units[a].strikesTwice && state.u[a].count > 0 && state.u[t].count > 0)
				Strike(state, a, t, state, outcome);
		}

		void Shoot(State& state, INT32 a, INT32 t, INT32 centre, Outcome& outcome)
		{
			if (!g_units[a].deathCloud)
			{
				Hit(state, t, RangedDamage(state, a, t), outcome);
				return;
			}
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			if (centre < 0 || centre >= HEX_COUNT)
				centre = state.u[t].head;
			INT32 victims[MAX_UNITS];
			INT32 count = 0;
			for (INT32 i = 0; i <= DIRECTIONS; ++i)
			{
				const INT32 hex = i == DIRECTIONS ? centre : Adj(centre, i);
				if (hex < 0 || hex >= HEX_COUNT || occupant[hex] < 0)
					continue;
				const INT32 x = occupant[hex];
				if (i != DIRECTIONS && !g_units[x].alive)
					continue;
				bool seen = false;
				for (INT32 j = 0; j < count; ++j)
					seen = seen || victims[j] == x;
				if (!seen)
					victims[count++] = x;
			}
			double damage[MAX_UNITS];
			for (INT32 i = 0; i < count; ++i)
				damage[i] = RangedDamage(state, a, victims[i]);
			for (INT32 i = 0; i < count; ++i)
				Hit(state, victims[i], damage[i], outcome);
		}

		void Shoot(State& state, INT32 a, INT32 t, Outcome& outcome)
		{
			Shoot(state, a, t, state.u[t].head, outcome);
		}

		double FightValueOfDamage(const State& state, INT32 target, double damage)
		{
			const UnitState& s = state.u[target];
			const double remainingHp = (s.count - 1) * static_cast<double>(g_units[target].maxHp) + s.topHp;
			const double dealt = damage < remainingHp ? damage : remainingHp;
			return dealt * g_units[target].fightValue / g_units[target].maxHp;
		}

		INT32 TotalHp(const State& state, INT32 u)
		{
			const UnitState& s = state.u[u];
			return s.count > 0 ? (s.count - 1) * g_units[u].maxHp + s.topHp : 0;
		}

		INT32 HealthLost(const State& state, INT32 u)
		{
			return state.u[u].count > 0 ? g_units[u].maxHp - state.u[u].topHp : 0;
		}

		INT32 GameStockDamage(INT32 a, INT32 t, INT32 count, bool ranged)
		{
			std::vector<INT32>& cache = ranged ? g_stockRangedDamage[a][t] : g_stockDamage[a][t];
			if (cache[count] < 0)
				cache[count] = THISCALL_6(INT32, STOCK_ESTIMATE_DAMAGE, g_units[a].stack, g_units[t].stack, ranged ? 1 : 0, count, 0, 0);
			return cache[count];
		}

		double StockPositionFactor(const State& state, INT32 a, INT32 t, bool ranged)
		{
			double factor = MoatDefence(state, a, t, ranged);
			if (!ranged)
				return factor;
			if (FarShot(a, g_start.u[a].head, t, g_start.u[t].head))
				factor *= 2.0;
			if (FarShot(a, state.u[a].head, t, state.u[t].head))
				factor *= 0.5;
			if (WallShot(g_start, a, g_start.u[a].head, t, g_start.u[t].head))
				factor *= 2.0;
			if (WallShot(state, a, state.u[a].head, t, state.u[t].head))
				factor *= 0.5;
			return factor;
		}

		INT32 StockDamageByCount(const State& state, INT32 a, INT32 t, INT32 count, INT32 targetHp, bool ranged = false)
		{
			const std::vector<INT32>& cache = ranged ? g_stockRangedDamage[a][t] : g_stockDamage[a][t];
			if (count >= static_cast<INT32>(cache.size()))
				count = static_cast<INT32>(cache.size()) - 1;
			if (count < 0)
				return 0;
			INT32 damage = g_task >= 0 ? TaskStockDamage(a, t, count, ranged) : GameStockDamage(a, t, count, ranged);
			if (damage > 0)
			{
				damage = static_cast<INT32>(damage * StockPositionFactor(state, a, t, ranged));
				if (damage < 1)
					damage = 1;
			}
			return damage < targetHp ? damage : targetHp;
		}

		INT32 StockDamage(const State& state, INT32 a, INT32 t, INT32 attackerHp, INT32 targetHp, bool ranged = false)
		{
			const INT32 maxHp = g_units[a].maxHp;
			return StockDamageByCount(state, a, t, (attackerHp + maxHp - 1) / maxHp, targetHp, ranged);
		}

		bool CanShootExcept(const State& state, INT32 u, INT32 excluded)
		{
			if (!g_units[u].shooter)
				return false;
			bool mask[HEX_COUNT];
			AdjacencyMask(u, state.u[u].head, mask);
			for (INT32 e = 0; e < g_unitCount; ++e)
				if (e != excluded && g_units[e].side != g_units[u].side && state.u[e].count > 0 && Touches(e, state.u[e].head, mask))
					return false;
			return true;
		}

		double StockFightValue(const State& state, INT32 u, bool ranged)
		{
			return ranged && CanShootExcept(state, u, -1) ? g_units[u].fvRanged : g_units[u].fvPerCreature;
		}

		INT32 StockCreatureValue(INT32 u, INT32 hpLost, INT32 healthLost, bool desperate, double fightValue)
		{
			const double perCreature = desperate ? DESPERATE_CREATURE_VALUE : fightValue;
			const INT32 maxHp = g_units[u].maxHp;
			const INT32 counted = hpLost + (hpLost % maxHp + healthLost >= maxHp ? healthLost : 0);
			return static_cast<INT32>(counted * perCreature / maxHp);
		}

		INT32 StockCreatureValue(INT32 u, INT32 hpLost, INT32 healthLost, bool desperate)
		{
			return StockCreatureValue(u, hpLost, healthLost, desperate, g_units[u].fvPerCreature);
		}

		INT32 StockAttackValue(const State& state, INT32 a, INT32 t, bool desperate, bool ranged = false)
		{
			const bool shot = ranged && CanShootExcept(state, a, -1);
			const INT32 attackerHp = TotalHp(state, a);
			const INT32 targetHp = TotalHp(state, t);
			INT32 attackerLeft = attackerHp;
			INT32 targetLeft = targetHp - StockDamage(state, a, t, attackerLeft, targetHp, shot);
			if (targetLeft > 0 && !shot)
			{
				if (g_stockCareful && !g_units[a].noRetaliation && !g_units[t].petrified && state.u[t].retaliations > 0)
					attackerLeft -= StockDamage(state, t, a, targetLeft, attackerLeft);
				if (attackerLeft > 0 && g_units[a].doubleAttack)
					targetLeft -= StockDamage(state, a, t, attackerLeft, targetLeft);
			}
			INT32 value = StockCreatureValue(t, targetHp - targetLeft, HealthLost(state, t), desperate, StockFightValue(state, t, shot));
			if (attackerLeft < attackerHp)
				value -= StockCreatureValue(a, attackerHp - attackerLeft, HealthLost(state, a), false, StockFightValue(state, a, shot));
			return value;
		}

		bool StockDesperate(const State& state, INT32 aiSide)
		{
			if (!g_stockCareful)
				return false;
			double own = 0.0;
			double other = 0.0;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				const INT32 strength = static_cast<INT32>(StockFightValue(state, u, true) * TotalHp(state, u) / g_units[u].maxHp);
				if (g_units[u].side == aiSide)
					own += strength;
				else
					other += strength;
			}
			return own * 2.0 < other;
		}

		bool CanShootNow(const State& state, INT32 u)
		{
			return g_units[u].shooter && !HasAdjacentEnemy(state, u);
		}

		double Net(const Outcome& outcome)
		{
			return outcome.gained - LOSS_WEIGHT * outcome.lost;
		}

		void FinishTurn(State& state, INT32 u)
		{
			state.u[u].done = true;
			state.lastSide = g_units[u].side;
		}

		double DamageCost(const State& state, INT32 u, double damage)
		{
			const UnitState& s = state.u[u];
			const double remainingHp = (s.count - 1) * static_cast<double>(g_units[u].maxHp) + s.topHp;
			const double dealt = damage < remainingHp ? damage : remainingHp;
			return dealt * g_units[u].value / g_units[u].maxHp;
		}

		bool CanAttackThisTurn(const State& state, INT32 u, INT32 t, const Occupancy& occupant)
		{
			INT32 distance[HEX_COUNT];
			Reach(u, state, occupant, g_units[u].speed, distance);
			bool mask[HEX_COUNT];
			AdjacencyMask(t, state.u[t].head, mask);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				if (distance[hex] >= 0 && Touches(u, hex, mask))
					return true;
			return false;
		}

		double RetaliationAbsorbed(const State& before, const State& after, INT32 a, INT32 t)
		{
			if (before.u[t].retaliations <= 0 || after.u[t].retaliations > 0 || after.u[t].count <= 0)
				return 0.0;
			Occupancy occupant;
			BuildOccupancy(after, occupant);
			double best = 0.0;
			for (INT32 b = 0; b < g_unitCount; ++b)
			{
				if (b == a || !Ours(b) || after.u[b].count <= 0 || after.u[b].done || g_units[b].noRetaliation)
					continue;
				if (!CanAttackThisTurn(after, b, t, occupant))
					continue;
				State trial = after;
				const INT32 countBefore = trial.u[t].count;
				ApplyDamage(trial, t, MeleeDamage(trial, b, t));
				const double killedValue = (countBefore - trial.u[t].count) * g_units[t].value;
				const double absorberLoss = (before.u[a].count - after.u[a].count) * g_units[a].value;
				if (trial.u[t].count <= 0 || killedValue <= absorberLoss)
					continue;
				const double saved = DamageCost(trial, b, MeleeDamage(trial, t, b));
				if (saved > best)
					best = saved;
			}
			return LOSS_WEIGHT * best;
		}
		void ApproachDistances(const State& state, INT32 side, INT32 (&goal)[HEX_COUNT], bool shootersOnly = false)
		{
			INT32 queue[HEX_COUNT];
			INT32 head = 0;
			INT32 tail = 0;
			const UINT8 breached = BreachBits(state);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				goal[hex] = -1;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				if (g_units[u].side != side || state.u[u].count <= 0 || (shootersOnly && !CanShootNow(state, u)))
					continue;
				INT32 hexes[2];
				const INT32 count = HexesAt(u, state.u[u].head, hexes);
				for (INT32 i = 0; i < count; ++i)
				{
					if (goal[hexes[i]] >= 0)
						continue;
					goal[hexes[i]] = 0;
					queue[tail++] = hexes[i];
				}
			}
			while (head < tail)
			{
				const INT32 hex = queue[head++];
				INT16 next[6];
				Neighbours(hex, next);
				for (INT16 n : next)
				{
					if (n < 0 || n >= HEX_COUNT || goal[n] >= 0 || !(g_standable[n] || Breached(n, breached)))
						continue;
					goal[n] = goal[hex] + 1;
					queue[tail++] = n;
				}
			}
		}

		INT32 AdvanceOnShooters(const State& state, INT32 a, const INT32 (&distance)[HEX_COUNT])
		{
			if (g_units[a].shooter)
				return -1;
			INT32 goal[HEX_COUNT];
			ApproachDistances(state, 1 - g_units[a].side, goal, true);
			auto gap = [&](INT32 head)
			{
				INT32 hexes[2];
				const INT32 count = HexesAt(a, head, hexes);
				INT32 best = FAR_AWAY;
				for (INT32 i = 0; i < count; ++i)
					if (goal[hexes[i]] >= 0 && goal[hexes[i]] < best)
						best = goal[hexes[i]];
				return best;
			};
			INT32 best = -1;
			INT32 bestGap = gap(state.u[a].head);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (distance[hex] < 0 || hex == state.u[a].head || MoatStop(state, a, hex))
					continue;
				const INT32 g = gap(hex);
				if (g < bestGap || (g == bestGap && best >= 0 && distance[hex] < distance[best]))
				{
					best = hex;
					bestGap = g;
				}
			}
			return best;
		}


		void AllyAct(State& state, INT32 a, Outcome& outcome)
		{
			state.u[a].defending = false;
			if (CanShootNow(state, a))
			{
				INT32 best = -1;
				double bestScore = -1.0;
				for (INT32 t = 0; t < g_unitCount; ++t)
				{
					if (Ours(t) || state.u[t].count <= 0)
						continue;
					State trial = state;
					Outcome o;
					Shoot(trial, a, t, o);
					const double score = o.gained + DAMAGE_TIEBREAK * RangedDamage(state, a, t);
					if (score > bestScore)
					{
						best = t;
						bestScore = score;
					}
				}
				if (best >= 0)
					Shoot(state, a, best, outcome);
				MoatHit(state, a, outcome);
				FinishTurn(state, a);
				return;
			}

			Occupancy occupant;
			BuildOccupancy(state, occupant);
			INT32 distance[HEX_COUNT];
			Reach(a, state, occupant, g_units[a].speed, distance);
			INT32 bestHex = -1;
			INT32 bestTarget = -1;
			double bestScore = 0.0;
			const INT32 start = state.u[a].head;
			for (INT32 t = 0; t < g_unitCount; ++t)
			{
				if (Ours(t) || state.u[t].count <= 0)
					continue;
				bool mask[HEX_COUNT];
				AdjacencyMask(t, state.u[t].head, mask);
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				{
					if (distance[hex] < 0 || !Touches(a, hex, mask) || (hex != start && MoatStop(state, a, hex)))
						continue;
					State trial = state;
					Outcome o;
					MoveTo(trial, a, hex, o);
					Exchange(trial, a, t, o);
					if (hex == start)
						MoatHit(trial, a, o);
					const double score = Net(o) + RetaliationAbsorbed(state, trial, a, t);
					if (score > bestScore)
					{
						bestHex = hex;
						bestTarget = t;
						bestScore = score;
					}
					break;
				}
			}
			if (bestTarget >= 0)
			{
				MoveTo(state, a, bestHex, outcome);
				Exchange(state, a, bestTarget, outcome);
				if (bestHex == start)
					MoatHit(state, a, outcome);
			}
			else
			{
				const INT32 advance = AdvanceOnShooters(state, a, distance);
				if (advance >= 0)
					MoveTo(state, a, advance, outcome);
				else
				{
					state.u[a].defending = true;
					MoatHit(state, a, outcome);
				}
			}
			FinishTurn(state, a);
		}

		struct EnemyPlan
		{
			INT32 target = -1;
			INT32 stand = -1;
			INT32 step = -1;
			INT32 hex = -1;
			INT32 turns = 0;
			INT32 score = 0;
			bool attackNow = false;
			bool shoot = false;
			bool wait = false;
			bool desperate = false;
		};

		struct StockPaths
		{
			INT16 distance[HEX_COUNT];
			INT8 dir[HEX_COUNT];
			INT8 blocked[HEX_COUNT];
			bool reached[HEX_COUNT];
			bool attackable[HEX_COUNT];
		};

		struct PathCacheEntry
		{
			INT32 generation;
			INT32 unit;
			INT32 maxDistance;
			INT32 costSpeed;
			INT32 stopSide;
			bool avoidMoat;
			UINT8 siegeKey;
			INT16 head[MAX_UNITS];
			StockPaths paths;
		};

		struct ThreatCacheEntry
		{
			INT32 generation;
			INT32 side;
			UINT64 avoidMask;
			UINT8 siegeKey;
			INT16 head[MAX_UNITS];
			INT16 threat[MAX_UNITS];
		};

		struct ThreadScratch
		{
			PathCacheEntry pathCache[PATH_CACHE_SIZE];
			ThreatCacheEntry threatCache[THREAT_CACHE_SIZE];
			INT32 guardReach[MAX_UNITS][HEX_COUNT];
			Candidate candidates[MAX_CANDIDATES];
			INT32 damageGeneration;
			std::vector<INT32> damage[MAX_UNITS][MAX_UNITS];
			std::vector<INT32> rangedDamage[MAX_UNITS][MAX_UNITS];
		};

		thread_local ThreadScratch* g_scratch = nullptr;

		ThreadScratch& Scratch()
		{
			if (!g_scratch)
				g_scratch = new ThreadScratch();
			return *g_scratch;
		}

		struct StockOption
		{
			INT32 target;
			INT32 stand;
			INT32 turns;
			INT32 score;
			INT32 hexValue;
			bool moveOnly;
		};

		void LogAppend(PCHAR log, size_t logSize, LPCSTR format, ...)
		{
			if (!log)
				return;
			const size_t used = strlen(log);
			if (used + 1 >= logSize)
				return;
			va_list args;
			va_start(args, format);
			_vsnprintf_s(log + used, logSize - used, _TRUNCATE, format, args);
			va_end(args);
		}

		bool StockFreeHex(INT32 u, INT32 hex, const Occupancy& occupant)
		{
			if (hex < 0 || hex >= HEX_COUNT)
				return false;
			const bool open = g_siege && IsGateHex(hex) ? !GateBlocked(1 - g_side, occupant) : g_stockOpen[hex] || Breached(hex, occupant.breached);
			return open && (occupant[hex] < 0 || occupant[hex] == u);
		}

		bool StockCanStand(INT32 u, INT32 hex, const Occupancy& occupant)
		{
			if (!StockFreeHex(u, hex, occupant))
				return false;
			return !g_units[u].wide || StockFreeHex(u, Adj(hex, FacingRight(u) ? 1 : 4), occupant);
		}

		bool StockStopHex(const State& state, INT32 u, INT32 hex)
		{
			if (hex < 0 || hex >= HEX_COUNT || !MoatHexNow(hex, GateOpen(state)))
				return false;
			return g_units[u].side != DEFENDER_SIDE || (hex != GATE_MOAT_HEX && hex != GATE_INNER_HEX);
		}

		bool StockAvoidsMoat(const State& state, INT32 u)
		{
			if (!g_moat || !g_units[u].moatWalker)
				return false;
			const INT32 hp = TotalHp(state, u);
			if (hp <= MOAT_AVOID_ALWAYS * g_moatTable)
				return true;
			if (hp > MOAT_AVOID_NEVER * g_moatTable)
				return false;
			return g_units[u].side == DEFENDER_SIDE || GateOpen(state);
		}

		void FindStockPathsMoat(const State& state, INT32 u, const Occupancy& occupant, INT32 maxDistance, StockPaths& out, bool avoid, INT32 costSpeed)
		{
			memset(&out, 0, sizeof(out));
			const Unit& unit = g_units[u];
			const INT32 speed = costSpeed >= 0 ? costSpeed : unit.speed;
			const INT32 start = state.u[u].head;
			const INT32 startTail = TailOf(u, start);
			auto stopHex = [&](INT32 hex)
			{
				return hex != start && hex != startTail && StockStopHex(state, u, hex);
			};
			INT32 queue[HEX_COUNT];
			bool queued[HEX_COUNT] = {};
			INT32 first = 0;
			INT32 pending = 0;
			auto push = [&](INT32 hex, INT32 dir, INT32 distance)
			{
				if (hex < 0 || hex >= HEX_COUNT || distance > maxDistance)
					return;
				if (out.reached[hex] && distance >= out.distance[hex])
					return;
				out.reached[hex] = true;
				out.distance[hex] = static_cast<INT16>(distance);
				out.dir[hex] = static_cast<INT8>(dir);
				if (!queued[hex])
				{
					queued[hex] = true;
					queue[(first + pending++) % HEX_COUNT] = hex;
				}
			};
			auto mark = [&](INT32 hex, INT32 distance)
			{
				if (hex < 0 || hex >= HEX_COUNT)
					return;
				if (!out.attackable[hex] || distance < out.distance[hex])
				{
					out.attackable[hex] = true;
					out.distance[hex] = static_cast<INT16>(distance);
				}
			};
			auto markStack = [&](INT32 x, INT32 distance)
			{
				mark(state.u[x].head, distance);
				if (g_units[x].wide)
					mark(TailOf(x, state.u[x].head), distance);
			};
			push(start, 0, 0);
			while (pending > 0)
			{
				const INT32 hex = queue[first];
				first = (first + 1) % HEX_COUNT;
				--pending;
				queued[hex] = false;
				const INT32 d = out.distance[hex];
				const INT32 tail = TailOf(u, hex);
				const bool headStop = stopHex(hex);
				const bool noAttack = maxDistance <= speed && (headStop || (unit.wide && stopHex(tail)));
				const INT32 attackDistance = headStop ? d + speed : d;
				for (INT32 dir = 0; dir < DIRECTIONS; ++dir)
				{
					const INT32 next = Adj(hex, dir);
					if (next < 0 || next >= HEX_COUNT)
						continue;
					bool stop = stopHex(next) && !(unit.wide && next == tail);
					if (unit.wide)
					{
						const INT32 nextTail = next + unit.tailOffset;
						if (nextTail >= 0 && nextTail < HEX_COUNT && nextTail != hex && stopHex(nextTail))
							stop = true;
					}
					if (StockCanStand(u, next, occupant) && !(avoid && stop))
					{
						push(next, dir, d + (stop && speed > 0 ? speed - d % speed : 1));
						continue;
					}
					if (noAttack || attackDistance > maxDistance)
						continue;
					const INT32 x = occupant[next];
					if (x >= 0 && g_units[x].side != unit.side && state.u[x].count > 0)
						markStack(x, attackDistance);
					if (unit.wide)
					{
						const INT32 other = next + unit.tailOffset;
						const INT32 y = other >= 0 && other < HEX_COUNT ? occupant[other] : -1;
						if (y >= 0 && g_units[y].side != unit.side && state.u[y].count > 0)
							markStack(y, attackDistance);
					}
				}
			}
		}

		void FindStockPathsUncached(const State& state, INT32 u, const Occupancy& occupant, INT32 maxDistance, StockPaths& out, INT32 stopSide)
		{
			INT32 unmarked = 0;
			bool counted[MAX_UNITS] = {};
			for (INT32 x = 0; x < g_unitCount; ++x)
				if (g_units[x].side == stopSide && state.u[x].count > 0)
					++unmarked;
			memset(&out, 0, sizeof(out));
			struct Entry
			{
				INT32 hex;
				INT32 distance;
				INT32 blocked;
			};
			Entry queue[HEX_COUNT];
			INT32 head = 0;
			INT32 tail = 0;
			const Unit& unit = g_units[u];
			auto push = [&](INT32 hex, INT32 dir, INT32 distance, INT32 blocked)
			{
				if (hex < 0 || hex >= HEX_COUNT || distance > maxDistance)
					return;
				if (out.reached[hex] && distance >= out.distance[hex])
					return;
				out.reached[hex] = true;
				out.distance[hex] = static_cast<INT16>(distance);
				out.dir[hex] = static_cast<INT8>(dir);
				out.blocked[hex] = static_cast<INT8>(blocked);
				if (tail < HEX_COUNT)
					queue[tail++] = { hex, distance, blocked };
			};
			auto mark = [&](INT32 hex, INT32 distance)
			{
				if (hex < 0 || hex >= HEX_COUNT)
					return;
				if (!out.attackable[hex] || distance < out.distance[hex])
				{
					out.attackable[hex] = true;
					out.distance[hex] = static_cast<INT16>(distance);
				}
			};
			auto markStack = [&](INT32 x, INT32 distance)
			{
				if (g_units[x].side == stopSide && !counted[x])
				{
					counted[x] = true;
					--unmarked;
				}
				mark(state.u[x].head, distance);
				if (g_units[x].wide)
					mark(TailOf(x, state.u[x].head), distance);
			};
			push(state.u[u].head, 0, 0, 0);
			const bool canStop = stopSide >= 0 && !unit.flyer && !unit.teleporter;
			while (head < tail && (!canStop || unmarked > 0))
			{
				const Entry entry = queue[head++];
				for (INT32 dir = 0; dir < DIRECTIONS; ++dir)
				{
					const INT32 next = Adj(entry.hex, dir);
					if (next < 0 || next >= HEX_COUNT)
						continue;
					if (StockCanStand(u, next, occupant))
					{
						push(next, dir, entry.distance + 1, 0);
						continue;
					}
					const INT32 x = occupant[next];
					if (x >= 0 && g_units[x].side != unit.side && state.u[x].count > 0)
						markStack(x, entry.distance);
					if (unit.wide)
					{
						const INT32 other = next + unit.tailOffset;
						const INT32 y = other >= 0 && other < HEX_COUNT ? occupant[other] : -1;
						if (y >= 0 && g_units[y].side != unit.side && state.u[y].count > 0)
							markStack(y, entry.distance);
					}
					if ((unit.flyer || unit.teleporter) && entry.blocked + 1 < unit.speed)
						push(next, dir, entry.distance + 1, entry.blocked + 1);
				}
			}
		}

		void FindStockPaths(const State& state, INT32 u, const Occupancy& occupant, INT32 maxDistance, StockPaths& out, INT32 stopSide = -1, INT32 costSpeed = -1)
		{
			PathCacheEntry* cache = Scratch().pathCache;
			INT16 key[MAX_UNITS];
			const bool avoid = StockAvoidsMoat(state, u);
			UINT32 hash = 2166136261u;
			hash = (hash ^ static_cast<UINT32>(u)) * 16777619u;
			hash = (hash ^ static_cast<UINT32>(maxDistance)) * 16777619u;
			hash = (hash ^ static_cast<UINT32>(costSpeed + 1)) * 16777619u;
			const UINT8 siegeKey = static_cast<UINT8>(BreachBits(state) | (GateOpen(state) ? 1u << BREACHES : 0u));
			hash = (hash ^ static_cast<UINT32>(stopSide + 1 + (avoid ? 0x100 : 0) + (siegeKey << 16))) * 16777619u;
			for (INT32 x = 0; x < g_unitCount; ++x)
			{
				key[x] = static_cast<INT16>(state.u[x].count > 0 ? state.u[x].head : -1);
				hash = (hash ^ static_cast<UINT16>(key[x])) * 16777619u;
			}
			PathCacheEntry& entry = cache[hash % PATH_CACHE_SIZE];
			if (entry.generation == g_cacheGeneration && entry.unit == u && entry.maxDistance == maxDistance && entry.costSpeed == costSpeed && entry.stopSide == stopSide
				&& entry.avoidMoat == avoid && entry.siegeKey == siegeKey && memcmp(entry.head, key, g_unitCount * sizeof(INT16)) == 0)
			{
				out = entry.paths;
				return;
			}
			if (g_moat && g_units[u].moatWalker)
				FindStockPathsMoat(state, u, occupant, maxDistance, out, avoid, costSpeed);
			else
				FindStockPathsUncached(state, u, occupant, maxDistance, out, stopSide);
			entry.generation = g_cacheGeneration;
			entry.unit = u;
			entry.maxDistance = maxDistance;
			entry.costSpeed = costSpeed;
			entry.stopSide = stopSide;
			entry.avoidMoat = avoid;
			entry.siegeKey = siegeKey;
			memcpy(entry.head, key, g_unitCount * sizeof(INT16));
			entry.paths = out;
		}

		void StockDangerMap(const State& state, INT32 e, bool desperate, const Occupancy& occupant, INT32 (&danger)[HEX_COUNT], bool (&threatened)[MAX_UNITS])
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				danger[hex] = 0;
			for (INT32 s = 0; s < MAX_UNITS; ++s)
				threatened[s] = false;
			if (g_moatDamage > 0)
			{
				const bool gateOpen = GateOpen(state);
				const INT32 moatValue = StockCreatureValue(e, g_moatDamage, HealthLost(state, e), desperate);
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
					if (MoatHexNow(hex, gateOpen))
						danger[hex] -= moatValue;
			}
			if (!g_stockCareful)
				return;
			const INT32 floor = -StockCreatureValue(e, TotalHp(state, e), HealthLost(state, e), false);
			StockPaths paths;
			for (INT32 s = 0; s < g_unitCount; ++s)
			{
				if (g_units[s].side == g_units[e].side || state.u[s].count <= 0 || g_units[s].disabled)
					continue;
				if (CanShootExcept(state, s, -1))
				{
					threatened[s] = true;
					continue;
				}
				FindStockPaths(state, s, occupant, g_units[s].speed + 1, paths, -1, g_units[s].speed + 1);
				bool threatening = false;
				for (INT32 x = 0; x < g_unitCount && !threatening; ++x)
				{
					if (g_units[x].side != g_units[e].side || state.u[x].count <= 0)
						continue;
					const INT32 hex = state.u[x].head;
					threatening = paths.attackable[hex] && paths.distance[hex] <= g_units[s].speed;
				}
				if (threatening)
				{
					threatened[s] = true;
					continue;
				}
				const INT32 penalty = -StockAttackValue(state, s, e, desperate);
				if (penalty >= 0)
					continue;
				auto add = [&](INT32 hex)
				{
					danger[hex] += penalty;
					if (danger[hex] < floor)
						danger[hex] = floor;
				};
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
					if (paths.reached[hex])
						add(hex);
				if (!g_units[s].wide)
					continue;
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				{
					if (!paths.reached[hex])
						continue;
					const INT32 back = Adj(hex, FacingRight(s) ? 1 : 4);
					if (back >= 0 && back < HEX_COUNT && !paths.reached[back])
						add(back);
				}
			}
		}

		INT32 StockBlockValue(const State& state, const Occupancy& occupant, INT32 e, INT32 hex, UINT32& counted)
		{
			if (!g_stockCareful || hex < 0 || hex >= HEX_COUNT)
				return 0;
			INT32 value = 0;
			for (INT32 dir = 0; dir < DIRECTIONS; ++dir)
			{
				const INT32 next = Adj(hex, dir);
				if (next < 0 || next >= HEX_COUNT)
					continue;
				const INT32 x = occupant[next];
				if (x < 0 || x == e || g_units[x].side == g_units[e].side)
					continue;
				const UINT32 bit = 1u << g_units[x].sideIndex;
				if (counted & bit)
					continue;
				counted |= bit;
				if (!CanShootExcept(state, x, e))
					continue;
				const INT32 hp = TotalHp(state, x);
				if (hp == 0)
					continue;
				const INT32 gain = static_cast<INT32>(hp * (StockFightValue(state, x, true) - g_units[x].fvPerCreature) / g_units[x].maxHp);
				value += gain < 1 ? 1 : gain;
			}
			return value;
		}

		INT32 StockAroundValue(const State& state, const Occupancy& occupant, INT32 a, INT32 aHead, INT32 count, INT32 t, INT32 tHead)
		{
			const UINT32 dirs = StrikeMask(a, aHead, t, tHead);
			UINT32 seen = 1u << g_units[t].sideIndex;
			INT32 value = 0;
			for (INT32 dir = 0; dir < WIDE_DIRECTIONS; ++dir)
			{
				if (!(dirs & (1u << dir)))
					continue;
				const INT32 hex = Around(a, aHead, dir);
				if (hex < 0 || hex >= HEX_COUNT)
					continue;
				const INT32 x = occupant[hex];
				if (x < 0 || g_units[x].side == g_units[a].side || (seen & (1u << g_units[x].sideIndex)))
					continue;
				const INT32 damage = StockDamageByCount(state, a, x, count, TotalHp(state, x));
				value += StockCreatureValue(x, damage, HealthLost(state, x), false);
				seen |= 1u << g_units[x].sideIndex;
			}
			return value;
		}

		INT32 StockBreathValue(const State& state, const Occupancy& occupant, INT32 side, INT32 a, INT32 aHead, INT32 count, INT32 t, INT32 tHead,
			bool desperate)
		{
			const INT32 dir = StockFacingDirection(a, aHead, t, tHead);
			if (dir < 0)
				return 0;
			const INT32 beyond = StepFrom(a, Around(a, aHead, dir), dir);
			if (beyond < 0 || beyond >= HEX_COUNT)
				return 0;
			const INT32 x = occupant[beyond];
			if (x < 0 || x == t)
				return 0;
			const INT32 damage = StockDamageByCount(state, a, x, count, TotalHp(state, x));
			const INT32 value = StockCreatureValue(x, damage, HealthLost(state, x), desperate);
			return g_units[x].side != side ? value : -value;
		}

		bool StockStand(const State& state, const Occupancy& occupant, INT32 e, INT32 t, const StockPaths& paths,
			const INT32 (&danger)[HEX_COUNT], bool desperate, INT32& stand, INT32& turns, INT32& value)
		{
			const INT32 speed = g_units[e].speed;
			const INT32 attackers = state.u[e].count;
			const INT32 targetHp = TotalHp(state, t);
			INT32 targetLeft = targetHp - StockDamageByCount(state, e, t, attackers, targetHp);
			if (targetLeft < 0)
				targetLeft = 0;
			const INT32 survivors = (targetLeft + g_units[t].maxHp - 1) / g_units[t].maxHp;
			const INT32 tHead = state.u[t].head;
			stand = -1;
			turns = 0;
			value = 0;
			auto scan = [&](INT32 centre, INT32 first, INT32 last)
			{
				if (centre < 0 || centre >= HEX_COUNT)
					return;
				for (INT32 dir = first; dir < last; ++dir)
				{
					const INT32 n = Adj(centre, dir);
					if (n < 0 || n >= HEX_COUNT || !paths.reached[n] || paths.blocked[n])
						continue;
					const INT32 d = paths.distance[n];
					INT32 nTurns;
					if (speed == 0)
						nTurns = d != 0 ? STOCK_IMMOBILE_TURNS : 1;
					else
					{
						nTurns = (d - 1 + speed) / speed;
						if (nTurns < 1)
							nTurns = 1;
					}
					if (stand >= 0 && nTurns > turns)
						continue;
					UINT32 counted = 0;
					INT32 v = StockBlockValue(state, occupant, e, n, counted);
					if (g_stockCareful)
					{
						if (g_units[e].allAround)
							v += StockAroundValue(state, occupant, e, n, attackers, t, tHead);
						if (g_units[e].breath)
							v += StockBreathValue(state, occupant, g_units[e].side, e, n, attackers, t, tHead, desperate);
						if (!g_units[e].noRetaliation && !g_units[t].petrified && state.u[t].retaliations > 0)
						{
							if (g_units[t].allAround)
								v -= StockAroundValue(state, occupant, t, tHead, survivors, e, n);
							if (g_units[t].breath)
								v -= StockBreathValue(state, occupant, g_units[t].side, t, tHead, survivors, e, n, desperate);
						}
					}
					INT32 hexDanger = danger[n];
					if (g_units[e].wide)
					{
						const INT32 back = n + g_units[e].tailOffset;
						v += StockBlockValue(state, occupant, e, back, counted);
						if (back >= 0 && back < HEX_COUNT && danger[back] < hexDanger)
							hexDanger = danger[back];
					}
					v += hexDanger;
					bool take = stand < 0 || nTurns != turns || v > value;
					if (!take && v == value)
					{
						const INT32 longer = d - paths.distance[stand];
						const bool jouster = g_units[e].type == NH3Creatures::CAVALIER || g_units[e].type == NH3Creatures::CHAMPION;
						take = jouster && g_stockCareful ? longer > 0 : longer < 0;
					}
					if (take)
					{
						stand = n;
						turns = nTurns;
						value = v;
					}
				}
			};
			scan(tHead, 0, DIRECTIONS);
			if (g_units[t].wide)
				scan(TailOf(t, tHead), 0, DIRECTIONS);
			if (g_units[e].wide)
			{
				const INT32 anchor = g_units[t].wide && g_units[t].tailOffset == -g_units[e].tailOffset ? TailOf(t, tHead) : tHead;
				if (g_units[e].tailOffset > 0)
					scan(Adj(anchor, 4), 3, DIRECTIONS);
				else
					scan(Adj(anchor, 1), 0, 3);
			}
			return stand >= 0;
		}

		INT32 StockWalk(const State& state, INT32 e, INT32 goal, const StockPaths& paths, const INT32 (&danger)[HEX_COUNT], bool careful, bool& wait)
		{
			wait = false;
			const INT32 start = state.u[e].head;
			const INT32 speed = g_units[e].speed;
			if (speed <= 0 || goal < 0 || goal >= HEX_COUNT || goal == start || !paths.reached[goal])
				return -1;
			INT32 route[HEX_COUNT];
			INT32 length = 0;
			for (INT32 hex = goal; hex != start; )
			{
				if (length >= HEX_COUNT || hex < 0 || hex >= HEX_COUNT || !paths.reached[hex])
					return -1;
				route[length++] = hex;
				hex = Adj(hex, (paths.dir[hex] + 3) % DIRECTIONS);
			}
			careful = careful && g_enemyCanWait && g_stockCareful;
			auto hexDanger = [&](INT32 hex)
			{
				INT32 v = danger[hex];
				const INT32 back = TailOf(e, hex);
				if (back >= 0 && back < HEX_COUNT && danger[back] < v)
					v = danger[back];
				return v;
			};
			const INT32 startDanger = hexDanger(start);
			INT32 best = startDanger;
			INT32 dest = start;
			INT32 last = start;
			bool moved = false;
			const INT32 base = (length - 1) / speed * speed;
			INT32 remaining = length;
			INT32 stepsLeft = speed;
			for (INT32 i = length - 1; i >= 0 && stepsLeft > 0; --i, --remaining, --stepsLeft)
			{
				const INT32 hex = route[i];
				last = hex;
				if (paths.blocked[hex])
					continue;
				const INT32 back = TailOf(e, hex);
				const bool early = (base < remaining || !moved) && !careful;
				const bool safe = best <= danger[hex] && (back < 0 || back >= HEX_COUNT || best <= danger[back]);
				if (early || safe)
				{
					dest = hex;
					moved = true;
					best = hexDanger(hex);
				}
			}
			if (careful && dest != last && startDanger <= best)
			{
				wait = true;
				return -1;
			}
			if (dest == start)
			{
				wait = careful;
				return -1;
			}
			return dest;
		}

		INT32 StockShotValue(const State& state, INT32 e, INT32 t, const INT32 (&threat)[MAX_UNITS], bool desperate)
		{
			const INT32 value = StockAttackValue(state, e, t, desperate, true);
			if (!g_stockCareful)
				return value;
			if (g_units[t].disabled)
				return value / STOCK_SHOT_DISABLED_DIVISOR;
			if (threat[t] > 0 && threat[t] < STOCK_SHOT_TURN_LIMIT)
				return value / threat[t];
			return value / STOCK_SHOT_FAR_DIVISOR;
		}

		INT32 StockCloudValue(const State& state, const Occupancy& occupant, INT32 e, INT32 hex, const INT32 (&threat)[MAX_UNITS], bool desperate)
		{
			bool seen[MAX_UNITS] = {};
			INT32 value = 0;
			for (INT32 i = 0; i <= DIRECTIONS; ++i)
			{
				const INT32 h = i == DIRECTIONS ? hex : Adj(hex, i);
				if (h < 0 || h >= HEX_COUNT)
					continue;
				const INT32 column = h % BATTLE_COLUMNS;
				if (column == 0 || column == BATTLE_COLUMNS - 1)
					continue;
				const INT32 x = occupant[h];
				if (x < 0 || seen[x])
					continue;
				seen[x] = true;
				if (g_units[e].undead && g_units[x].undead && state.u[x].head != hex && TailOf(x, state.u[x].head) != hex)
					continue;
				if (g_units[x].side == g_units[e].side)
					value -= StockAttackValue(state, e, x, desperate, true);
				else
					value += StockShotValue(state, e, x, threat, desperate);
			}
			return value;
		}

		INT32 NearestDeadStack(const StockPaths& paths, INT32 speed, const bool (&deadHex)[HEX_COUNT], INT32 nearest)
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				if (deadHex[hex] && paths.reached[hex] && paths.blocked[hex] == 0 && paths.distance[hex] <= speed && (nearest < 0 || paths.distance[hex] < nearest))
					nearest = paths.distance[hex];
			return nearest;
		}

		INT32 StockThreatDistance(const State& state, INT32 u, const Occupancy& occupant, INT32 aiSide, const bool (&deadHex)[HEX_COUNT])
		{
			const Unit& unit = g_units[u];
			if (g_moat && unit.moatWalker)
			{
				StockPaths paths;
				FindStockPaths(state, u, occupant, STOCK_FULL_DISTANCE, paths);
				INT32 nearest = -1;
				for (INT32 x = 0; x < g_unitCount; ++x)
				{
					if (g_units[x].side != aiSide || state.u[x].count <= 0)
						continue;
					INT32 hexes[2];
					const INT32 count = HexesAt(x, state.u[x].head, hexes);
					for (INT32 i = 0; i < count; ++i)
						if (paths.attackable[hexes[i]] && (nearest < 0 || paths.distance[hexes[i]] < nearest))
							nearest = paths.distance[hexes[i]];
				}
				return NearestDeadStack(paths, unit.speed, deadHex, nearest);
			}
			INT16 distance[HEX_COUNT];
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				distance[hex] = -1;
			INT16 queue[HEX_COUNT];
			INT32 head = 0;
			INT32 tail = 0;
			distance[state.u[u].head] = 0;
			if (deadHex[state.u[u].head])
				return 0;
			queue[tail++] = static_cast<INT16>(state.u[u].head);
			auto enemyAt = [&](INT32 hex)
			{
				if (hex < 0 || hex >= HEX_COUNT)
					return false;
				const INT32 x = occupant[hex];
				return x >= 0 && g_units[x].side == aiSide && state.u[x].count > 0;
			};
			while (head < tail)
			{
				const INT32 hex = queue[head++];
				for (INT32 dir = 0; dir < DIRECTIONS; ++dir)
				{
					const INT32 next = Adj(hex, dir);
					if (next < 0 || next >= HEX_COUNT)
						continue;
					if (StockCanStand(u, next, occupant))
					{
						if (distance[next] < 0 && distance[hex] + 1 <= STOCK_FULL_DISTANCE)
						{
							distance[next] = static_cast<INT16>(distance[hex] + 1);
							if (deadHex[next] && distance[next] <= unit.speed)
								return distance[next];
							queue[tail++] = static_cast<INT16>(next);
						}
						continue;
					}
					if (enemyAt(next) || (unit.wide && enemyAt(next + unit.tailOffset)))
						return distance[hex];
				}
			}
			return -1;
		}

		void StockThreatsUncached(const State& state, const Occupancy& occupant, INT32 aiSide, INT32 (&threat)[MAX_UNITS])
		{
			StockPaths paths;
			bool deadHex[HEX_COUNT];
			memcpy(deadHex, g_deadStackHex[aiSide], sizeof(deadHex));
			for (INT32 x = 0; x < g_unitCount; ++x)
				if (g_units[x].side == aiSide && state.u[x].count <= 0 && state.u[x].head >= 0 && state.u[x].head < HEX_COUNT)
					deadHex[state.u[x].head] = true;
			for (INT32 s = 0; s < g_unitCount; ++s)
			{
				threat[s] = 0;
				if (g_units[s].side == aiSide || state.u[s].count <= 0 || !g_units[s].threatens)
					continue;
				const INT32 speed = g_units[s].speed;
				INT32 nearest = -1;
				if (CanShootExcept(state, s, -1))
				{
					for (INT32 o = 0; o < g_unitCount && nearest < 0; ++o)
						if (g_units[o].side == aiSide)
							nearest = 1;
				}
				else if (g_units[s].flyer || g_units[s].teleporter)
				{
					FindStockPaths(state, s, occupant, STOCK_FULL_DISTANCE, paths, aiSide);
					for (INT32 o = 0; o < g_unitCount; ++o)
					{
						if (g_units[o].side != aiSide || state.u[o].count <= 0 || !paths.attackable[state.u[o].head])
							continue;
						const INT32 d = paths.distance[state.u[o].head];
						if (nearest < 0 || d < nearest)
							nearest = d;
					}
					nearest = NearestDeadStack(paths, speed, deadHex, nearest);
				}
				else
					nearest = StockThreatDistance(state, s, occupant, aiSide, deadHex);
				if (nearest < 0 || (speed == 0 && nearest != 0))
					continue;
				const INT32 turns = speed == 0 ? 1 : (speed - 1 + nearest) / speed;
				threat[s] = turns < 1 ? 1 : turns;
			}
		}

		void StockThreats(const State& state, const Occupancy& occupant, INT32 aiSide, INT32 (&threat)[MAX_UNITS])
		{
			ThreatCacheEntry* cache = Scratch().threatCache;
			INT16 key[MAX_UNITS];
			UINT64 avoidMask = 0;
			UINT32 hash = 2166136261u ^ static_cast<UINT32>(aiSide);
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				key[u] = static_cast<INT16>(state.u[u].count > 0 ? state.u[u].head : -2 - state.u[u].head);
				hash = (hash ^ static_cast<UINT16>(key[u])) * 16777619u;
				if (StockAvoidsMoat(state, u))
					avoidMask |= 1ull << u;
			}
			if (avoidMask)
				hash = (hash ^ static_cast<UINT32>(avoidMask ^ (avoidMask >> 32))) * 16777619u;
			const UINT8 siegeKey = static_cast<UINT8>(occupant.breached | (occupant.gateOpen ? 1u << BREACHES : 0u));
			if (siegeKey)
				hash = (hash ^ siegeKey) * 16777619u;
			ThreatCacheEntry& entry = cache[hash % THREAT_CACHE_SIZE];
			if (entry.generation == g_cacheGeneration && entry.side == aiSide && entry.avoidMask == avoidMask && entry.siegeKey == siegeKey
				&& memcmp(entry.head, key, g_unitCount * sizeof(INT16)) == 0)
			{
				for (INT32 u = 0; u < g_unitCount; ++u)
					threat[u] = entry.threat[u];
				return;
			}
			StockThreatsUncached(state, occupant, aiSide, threat);
			entry.generation = g_cacheGeneration;
			entry.side = aiSide;
			entry.avoidMask = avoidMask;
			entry.siegeKey = siegeKey;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				entry.head[u] = key[u];
				entry.threat[u] = static_cast<INT16>(threat[u]);
			}
		}

		void PlanShot(const State& state, const Occupancy& occupant, INT32 e, EnemyPlan& plan, PCHAR log, size_t logSize)
		{
			INT32 threat[MAX_UNITS];
			StockThreats(state, occupant, g_units[e].side, threat);
			for (INT32 pass = 0; pass < 2; ++pass)
			{
				INT32 best = -1;
				INT32 bestValue = 0;
				INT32 bestHex = -1;
				for (INT32 t = 0; t < g_unitCount; ++t)
				{
					if (g_units[t].side == g_units[e].side || state.u[t].count <= 0)
						continue;
					INT32 hex = state.u[t].head;
					INT32 value;
					if (g_units[e].cloudShooter)
					{
						value = StockCloudValue(state, occupant, e, hex, threat, plan.desperate);
						if (g_units[t].wide)
						{
							const INT32 back = TailOf(t, hex);
							const INT32 backValue = StockCloudValue(state, occupant, e, back, threat, plan.desperate);
							if (value < backValue)
							{
								hex = back;
								value = backValue;
							}
						}
					}
					else
						value = StockShotValue(state, e, t, threat, plan.desperate);
					LogAppend(log, logSize, "\n      port shot %s x%d at %d: hex %d threat turns %d value %d", g_units[t].stack->GetCreatureName(),
						state.u[t].count, state.u[t].head, hex, threat[t], value);
					if (g_units[e].cloudShooter && value < 0)
						continue;
					if (best >= 0)
					{
						const bool disabled = g_units[t].disabled;
						const bool bestDisabled = g_units[best].disabled;
						if (disabled && !bestDisabled)
							continue;
						if (disabled == bestDisabled && value < bestValue)
							continue;
					}
					best = t;
					bestValue = value;
					bestHex = hex;
				}
				if (pass == 0 && plan.desperate && best >= 0 && bestValue == 0)
				{
					plan.desperate = false;
					continue;
				}
				if (best >= 0)
				{
					plan.shoot = true;
					plan.target = best;
					plan.hex = bestHex;
				}
				return;
			}
		}

		bool StockAreaThreatZero(const State& state, INT32 e, const bool (&threatened)[MAX_UNITS])
		{
			if (g_units[e].heroBlast)
				return false;
			for (INT32 s = 0; s < g_unitCount; ++s)
				if (threatened[s] && state.u[s].count > 0 && ((g_units[s].fireball && CanShootExcept(state, s, -1)) || g_units[s].breath))
					return false;
			return true;
		}

		bool StockGuardHex(const State& state, const Occupancy& occupant, INT32 e, INT32 x, const StockPaths& paths, INT32& hex, INT32& free)
		{
			hex = -1;
			free = 0;
			INT32 bestTurns = 0;
			INT32 bestWidth = 0;
			const INT32 speed = g_units[e].speed;
			const INT32 xHead = state.u[x].head;
			for (INT32 dir = 0; dir < DirectionCount(x); ++dir)
			{
				const INT32 h = Around(x, xHead, dir);
				if (h < 0 || h >= HEX_COUNT || (occupant[h] >= 0 && occupant[h] != e))
					continue;
				++free;
				if (!paths.reached[h])
					continue;
				INT32 turns = speed > 0 ? (paths.distance[h] - 1 + speed) / speed : 1;
				if (turns < 1)
					turns = 1;
				INT32 width = 1;
				if (g_units[e].wide)
				{
					const INT32 back = h + g_units[e].tailOffset;
					INT32 xHexes[2];
					const INT32 xCount = HexesAt(x, xHead, xHexes);
					for (INT32 i = 0; i < xCount && width == 1; ++i)
						for (INT32 k = 0; k < DIRECTIONS; ++k)
							if (back >= 0 && back < HEX_COUNT && Adj(xHexes[i], k) == back)
								width = 2;
				}
				bool take = hex < 0 || turns < bestTurns;
				if (!take && turns == bestTurns)
				{
					if (width != bestWidth)
						take = width > bestWidth;
					else if (g_units[x].facing == 1)
						take = g_combat->squares[hex].x <= g_combat->squares[h].x;
					else
						take = g_combat->squares[h].x <= g_combat->squares[hex].x;
				}
				if (take)
				{
					hex = h;
					bestTurns = turns;
					bestWidth = width;
				}
			}
			return hex >= 0;
		}

		bool StockGuardShooters(const State& state, const Occupancy& occupant, INT32 e, const StockPaths& paths, EnemyPlan& plan)
		{
			INT32 best = -1;
			INT32 bestHex = -1;
			INT32 bestTurns = 0;
			INT32 bestValue = 0;
			INT32 lastHex = -1;
			const INT32 speed = g_units[e].speed;
			for (INT32 x = 0; x < g_unitCount; ++x)
			{
				if (g_units[x].side != g_units[e].side || state.u[x].count <= 0 || g_units[x].noMeleePenalty || !CanShootExcept(state, x, -1))
					continue;
				INT32 hex;
				INT32 free;
				const bool found = StockGuardHex(state, occupant, e, x, paths, hex, free);
				lastHex = hex;
				if (!found)
					continue;
				INT32 turns = speed > 0 ? (paths.distance[hex] - 1 + speed) / speed : 1;
				if (turns < 1)
					turns = 1;
				INT32 value = static_cast<INT32>(StockFightValue(state, x, true) * TotalHp(state, x) / g_units[x].maxHp);
				if (free > 1)
					value /= free;
				if (best < 0 || turns < bestTurns || (turns == bestTurns && bestValue <= value))
				{
					best = x;
					bestHex = hex;
					bestTurns = turns;
					bestValue = value;
				}
			}
			if (best < 0)
				return false;
			if (lastHex != state.u[e].head)
			{
				static const INT32 calm[HEX_COUNT] = {};
				plan.step = StockWalk(state, e, bestHex, paths, calm, false, plan.wait);
			}
			return true;
		}

		INT32 StockSelect(const StockOption* options, INT32 optionCount, const INT32* rolls, const StockPaths& paths, const State& state)
		{
			INT32 best = -1;
			INT32 stored = 0;
			for (INT32 i = 0; i < optionCount; ++i)
			{
				const StockOption& o = options[i];
				const INT32 score = rolls[i] * o.score / 100;
				if (best >= 0)
				{
					const StockOption& b = options[best];
					const bool disabled = g_units[o.target].disabled;
					const bool bestDisabled = g_units[b.target].disabled;
					if (disabled && !bestDisabled)
						continue;
					if (disabled == bestDisabled)
					{
						if (b.turns < o.turns)
							continue;
						if (b.turns == o.turns)
						{
							if (score < stored)
								continue;
							if (score == stored)
							{
								const INT32 lost = HealthLost(state, o.target);
								const INT32 bestLost = HealthLost(state, b.target);
								if (lost < bestLost || (lost == bestLost && paths.distance[b.stand] < paths.distance[o.stand]))
									continue;
							}
						}
					}
				}
				best = i;
				stored = score / o.turns;
			}
			return best;
		}

		struct EnemyReplies
		{
			EnemyPlan plans[ROBUST_REPLIES];
			INT32 count = 0;
		};

		bool StayInside(const State& state)
		{
			if (!g_stayInside)
				return false;
			for (INT32 i = 0; i < static_cast<INT32>(sizeof(STAY_INSIDE_ELEMENTS) / sizeof(STAY_INSIDE_ELEMENTS[0])); ++i)
				if (g_stayTarget[i] >= 0 && state.fort[g_stayTarget[i]] <= 0 && !g_stayHexBlocked[i])
					return false;
			return true;
		}

		EnemyPlan PlanEnemy(const State& state, INT32 e, PCHAR log = nullptr, size_t logSize = 0, EnemyReplies* replies = nullptr)
		{
			EnemyPlan plan;
			if (replies)
				replies->count = 0;
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			plan.desperate = StockDesperate(state, g_units[e].side);
			if (CanShootNow(state, e))
			{
				PlanShot(state, occupant, e, plan, log, logSize);
				return plan;
			}

			StockPaths paths;
			FindStockPaths(state, e, occupant, STOCK_FULL_DISTANCE, paths);
			INT32 danger[HEX_COUNT];
			bool threatened[MAX_UNITS];
			StockDangerMap(state, e, plan.desperate, occupant, danger, threatened);
			if (log)
			{
				LogAppend(log, logSize, "\n      port danger map:");
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
					if (danger[hex] != 0)
						LogAppend(log, logSize, " %d:%d", hex, danger[hex]);
			}

			StockOption options[MAX_UNITS];
			INT32 optionCount = 0;
			bool stayInside = StayInside(state) && g_units[e].side == DEFENDER_SIDE;
			for (INT32 x = 0; x < g_unitCount && stayInside; ++x)
				if (g_units[x].side == DEFENDER_SIDE && state.u[x].count > 0 && !Inside(state.u[x].head))
					stayInside = false;
			for (INT32 t = 0; t < g_unitCount; ++t)
			{
				if (g_units[t].side == g_units[e].side || state.u[t].count <= 0)
					continue;
				if (g_units[e].speed == 0 && paths.distance[state.u[t].head] != 0)
					continue;
				if (stayInside && !Inside(state.u[t].head) && !(g_units[t].wide && Inside(TailOf(t, state.u[t].head))))
					continue;
				INT32 stand;
				INT32 turns;
				INT32 hexValue;
				if (!StockStand(state, occupant, e, t, paths, danger, plan.desperate, stand, turns, hexValue))
					continue;
				const INT32 attack = StockAttackValue(state, e, t, plan.desperate);
				INT32 score = attack;
				bool moveOnly = false;
				if (hexValue < 1 || g_units[e].summoned || !g_stockCareful)
					score += hexValue;
				else if (g_units[e].harpy)
				{
					if (score < hexValue)
					{
						moveOnly = true;
						score = hexValue;
					}
				}
				else if (score >= 0 || !g_strongerAtRange)
					score += hexValue;
				else
				{
					moveOnly = true;
					score = hexValue;
				}
				const bool eligible = score >= 0 || !g_stockCareful || danger[stand] <= hexValue;
				LogAppend(log, logSize, "\n      port target %s x%d at %d: stand %d turns %d hex value %d attack %d score %d%s",
					g_units[t].stack->GetCreatureName(), state.u[t].count, state.u[t].head, stand, turns, hexValue, attack, score,
					eligible ? (moveOnly ? " move only" : "") : " skipped");
				if (eligible)
					options[optionCount++] = { t, stand, turns, score, hexValue, moveOnly };
			}
			const bool guardShooters = g_stockCareful && g_strongerAtRange && StockAreaThreatZero(state, e, threatened);
			if (optionCount == 0)
			{
				if (!guardShooters || !StockGuardShooters(state, occupant, e, paths, plan))
					plan.wait = g_enemyCanWait;
				return plan;
			}

			INT32 wins[MAX_UNITS] = {};
			INT32 guardVotes[MAX_UNITS] = {};
			INT32 rolls[MAX_UNITS];
			DWORD seed = STOCK_ROLL_SEED;
			for (INT32 sample = 0; sample < STOCK_ROLL_SAMPLES; ++sample)
			{
				for (INT32 i = 0; i < optionCount; ++i)
				{
					seed = seed * 214013 + 2531011;
					rolls[i] = STOCK_ROLL_LOW + static_cast<INT32>((seed >> 16) % (STOCK_ROLL_HIGH - STOCK_ROLL_LOW + 1));
				}
				const INT32 winner = StockSelect(options, optionCount, rolls, paths, state);
				if (winner < 0)
					continue;
				++wins[winner];
				const StockOption& w = options[winner];
				const INT32 stored = rolls[winner] * w.score / 100 / w.turns;
				const INT32 hexPart = w.hexValue * rolls[winner] / (w.turns * 100);
				if (stored < 0 && hexPart > stored && !g_units[e].summoned)
					++guardVotes[winner];
			}
			INT32 order[MAX_UNITS];
			for (INT32 i = 0; i < optionCount; ++i)
				order[i] = i;
			std::stable_sort(order, order + optionCount, [&wins](INT32 a, INT32 b) { return wins[a] > wins[b]; });
			if (log)
			{
				LogAppend(log, logSize, "\n      port roll wins:");
				for (INT32 i = 0; i < optionCount; ++i)
					LogAppend(log, logSize, " %s@%d %d/%d", g_units[options[i].target].stack->GetCreatureName(), state.u[options[i].target].head,
						wins[i], STOCK_ROLL_SAMPLES);
			}

			auto finish = [&](INT32 chosen)
			{
				EnemyPlan result = plan;
				const StockOption& c = options[chosen];
				result.target = c.target;
				result.stand = c.stand;
				result.turns = c.turns;
				result.score = c.score;
				if (!c.moveOnly && guardShooters && guardVotes[chosen] * 2 > wins[chosen])
				{
					EnemyPlan guard = plan;
					if (StockGuardShooters(state, occupant, e, paths, guard))
						return guard;
				}
				if (!c.moveOnly && c.turns < 2)
					result.attackNow = true;
				else if (c.stand == state.u[e].head)
					result.wait = g_enemyCanWait;
				else
					result.step = StockWalk(state, e, c.stand, paths, danger, c.turns >= 2, result.wait);
				return result;
			};
			if (replies)
			{
				replies->count = 0;
				for (INT32 i = 0; i < optionCount && replies->count < ROBUST_REPLIES; ++i)
					if (i == 0 || wins[order[i]] > 0)
						replies->plans[replies->count++] = finish(order[i]);
			}
			return finish(order[0]);
		}

		void EnemyAct(State& state, INT32 e, Outcome& outcome);

		void MoraleRisk(const State& state, INT32 e, Outcome& outcome)
		{
			if (g_inMoraleAction || g_units[e].morale <= 0 || state.u[e].count <= 0 || !state.u[e].done)
				return;
			const INT32 points = g_units[e].morale < MORALE_CAP ? g_units[e].morale : MORALE_CAP;
			const double chance = points * MORALE_CHANCE_PER_POINT;
			State extra = state;
			extra.u[e].done = false;
			Outcome o;
			g_inMoraleAction = true;
			EnemyAct(extra, e, o);
			g_inMoraleAction = false;
			outcome.gained += chance * o.gained;
			outcome.lost += chance * o.lost;
		}

		void EnemyActPlan(State& state, INT32 e, const EnemyPlan& plan, Outcome& outcome)
		{
			state.u[e].defending = false;
			if (plan.shoot)
			{
				Shoot(state, e, plan.target, plan.hex, outcome);
				MoatHit(state, e, outcome);
			}
			else if (plan.attackNow)
			{
				const bool inPlace = plan.stand == state.u[e].head;
				MoveTo(state, e, plan.stand, outcome);
				Exchange(state, e, plan.target, outcome);
				if (inPlace)
					MoatHit(state, e, outcome);
			}
			else if (plan.step >= 0)
			{
				MoveTo(state, e, plan.step, outcome);
			}
			else if (plan.wait && !state.waitPhase && !state.u[e].waited)
			{
				state.u[e].waited = true;
				state.lastSide = g_units[e].side;
				MoatHit(state, e, outcome);
				return;
			}
			else
			{
				state.u[e].defending = true;
				MoatHit(state, e, outcome);
			}
			FinishTurn(state, e);
			if (!plan.shoot && !plan.attackNow && plan.step < 0)
				return;
			MoraleRisk(state, e, outcome);
		}

		void EnemyAct(State& state, INT32 e, Outcome& outcome)
		{
			EnemyActPlan(state, e, PlanEnemy(state, e), outcome);
		}

		double Exposure(const State& state)
		{
			double total = 0.0;
			for (INT32 e = 0; e < g_unitCount; ++e)
			{
				if (Ours(e) || state.u[e].count <= 0)
					continue;
				const EnemyPlan plan = PlanEnemy(state, e);
				if (!plan.shoot && !plan.attackNow)
					continue;
				State trial = state;
				Outcome o;
				if (plan.shoot)
					Shoot(trial, e, plan.target, plan.hex, o);
				else
				{
					trial.u[e].head = plan.stand;
					Exchange(trial, e, plan.target, o);
				}
				total += o.lost;
			}
			return total;
		}

		bool ActsBefore(const State& state, INT32 a, INT32 b, bool waitPhase)
		{
			const INT32 speedA = g_units[a].speed;
			const INT32 speedB = g_units[b].speed;
			if (speedA != speedB)
				return waitPhase ? speedA < speedB : speedA > speedB;
			if (g_units[a].side != g_units[b].side)
				return g_units[a].side != state.lastSide;
			return g_units[a].sideIndex < g_units[b].sideIndex;
		}

		INT32 NextActor(State& state)
		{
			for (INT32 phase = 0; phase < 2; ++phase)
			{
				const bool waitPhase = state.waitPhase || phase == 1;
				INT32 best = -1;
				for (INT32 u = 0; u < g_unitCount; ++u)
				{
					const UnitState& s = state.u[u];
					if (s.count <= 0 || s.done || (!waitPhase && s.waited) || (waitPhase && !s.waited))
						continue;
					if (best < 0 || ActsBefore(state, u, best, waitPhase))
						best = u;
				}
				if (best >= 0)
				{
					state.waitPhase = waitPhase;
					return best;
				}
				if (state.waitPhase)
					break;
			}
			return -1;
		}

		bool SideAlive(const State& state, INT32 side)
		{
			for (INT32 u = 0; u < g_unitCount; ++u)
				if (g_units[u].side == side && state.u[u].count > 0)
					return true;
			return false;
		}

		void Trace(LPCSTR format, ...)
		{
			if (!g_trace)
				return;
			const size_t used = strlen(g_trace);
			if (used + 1 >= g_traceSize)
				return;
			va_list args;
			va_start(args, format);
			_vsnprintf_s(g_trace + used, g_traceSize - used, _TRUNCATE, format, args);
			va_end(args);
		}

		void TraceAction(const State& before, const State& after, INT32 actor)
		{
			if (!g_trace)
				return;
			Trace("\n      sim r%d %s %s x%d %d->%d", before.round, Ours(actor) ? "ours" : "enemy",
				g_units[actor].stack->GetCreatureName(), before.u[actor].count, before.u[actor].head, after.u[actor].head);
			if (after.u[actor].waited && !after.u[actor].done)
				Trace(" wait");
			else if (after.u[actor].defending)
				Trace(" defend");
			for (INT32 u = 0; u < g_unitCount; ++u)
				if (before.u[u].count != after.u[u].count)
					Trace(" | %s x%d->x%d", g_units[u].stack->GetCreatureName(), before.u[u].count, after.u[u].count);
			if (GateOpen(before) != GateOpen(after))
				Trace(" | gate %s", GateOpen(after) ? "open" : "closed");
		}

		void SearchAct(State& state, INT32 a, Outcome& outcome);

		bool Augment(INT32 u, const INT32 (*reach)[HEX_COUNT], const bool (&open)[HEX_COUNT], INT32 (&matched)[HEX_COUNT], bool (&seen)[HEX_COUNT])
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (!open[hex] || seen[hex] || reach[u][hex] < 0)
					continue;
				seen[hex] = true;
				if (matched[hex] < 0 || Augment(matched[hex], reach, open, matched, seen))
				{
					matched[hex] = u;
					return true;
				}
			}
			return false;
		}

		bool GuardHexes(const State& state, const GuardPlan& plan, const Occupancy& occupant, bool (&open)[HEX_COUNT], bool (&wall)[HEX_COUNT])
		{
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				open[hex] = wall[hex] = false;
			for (INT32 p = 0; p < g_unitCount; ++p)
			{
				if (!plan.guarded[p] || state.u[p].count <= 0)
					continue;
				bool mask[HEX_COUNT];
				AdjacencyMask(p, state.u[p].head, mask);
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
					if (mask[hex] && HeadOk(hex, occupant) && (occupant[hex] < 0 || !plan.guarded[occupant[hex]]))
						wall[hex] = true;
			}
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (!wall[hex])
					continue;
				if (occupant[hex] < 0)
					open[hex] = true;
				else if (!Ours(occupant[hex]))
					return false;
			}
			return true;
		}

		INT32 FreeNeighbours(INT32 hex, const Occupancy& occupant)
		{
			INT16 next[6];
			Neighbours(hex, next);
			INT32 count = 0;
			for (INT16 n : next)
				if (n >= 0 && n < HEX_COUNT && HeadOk(n, occupant) && occupant[n] < 0)
					++count;
			return count;
		}

		INT32 GuardPick(const State& state, const GuardPlan& plan, INT32 a)
		{
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			bool open[HEX_COUNT];
			bool wall[HEX_COUNT];
			if (!GuardHexes(state, plan, occupant, open, wall))
				return -1;
			INT32 hexes[2];
			const INT32 count = HexesAt(a, state.u[a].head, hexes);
			for (INT32 i = 0; i < count; ++i)
				if (wall[hexes[i]])
					return state.u[a].head;
			INT32 distance[HEX_COUNT];
			Reach(a, state, occupant, g_units[a].speed, distance);

			INT32 (&reach)[MAX_UNITS][HEX_COUNT] = Scratch().guardReach;
			INT32 others[MAX_UNITS];
			INT32 otherCount = 0;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				if (u == a || !plan.mover[u] || state.u[u].count <= 0 || state.u[u].done)
					continue;
				Reach(u, state, occupant, g_units[u].speed, reach[u]);
				others[otherCount++] = u;
			}
			auto restCanBeFilled = [&](INT32 taken)
			{
				bool rest[HEX_COUNT];
				INT32 restCount = 0;
				INT32 matched[HEX_COUNT];
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				{
					rest[hex] = open[hex] && hex != taken;
					matched[hex] = -1;
					if (rest[hex])
						++restCount;
				}
				INT32 matchedCount = 0;
				for (INT32 i = 0; i < otherCount && matchedCount < restCount; ++i)
				{
					bool seen[HEX_COUNT] = {};
					if (Augment(others[i], reach, rest, matched, seen))
						++matchedCount;
				}
				return matchedCount == restCount;
			};

			INT32 best = -1;
			INT32 bestFree = 0;
			bool bestFills = false;
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (!open[hex] || distance[hex] < 0)
					continue;
				const bool fills = restCanBeFilled(hex);
				const INT32 free = FreeNeighbours(hex, occupant);
				if (best < 0 || (fills && !bestFills)
					|| (fills == bestFills && (free < bestFree || (free == bestFree && distance[hex] < distance[best]))))
				{
					best = hex;
					bestFree = free;
					bestFills = fills;
				}
			}
			return best;
		}

		bool GuardAct(State& state, INT32 a, Outcome& outcome)
		{
			if (!g_guard || state.round != g_startRound)
				return false;
			if (g_guard->guarded[a])
			{
				state.u[a].defending = true;
				MoatHit(state, a, outcome);
				FinishTurn(state, a);
				return true;
			}
			if (!g_guard->mover[a])
				return false;
			const INT32 hex = GuardPick(state, *g_guard, a);
			if (hex < 0)
				return false;
			state.u[a].defending = hex == state.u[a].head;
			if (state.u[a].defending)
				MoatHit(state, a, outcome);
			else
				MoveTo(state, a, hex, outcome);
			FinishTurn(state, a);
			return true;
		}

		INT32 TowerTarget(const State& state, INT32 count)
		{
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			INT32 threat[MAX_UNITS];
			StockThreats(state, occupant, DEFENDER_SIDE, threat);
			bool desperate = StockDesperate(state, DEFENDER_SIDE);
			for (INT32 pass = 0; pass < 2; ++pass)
			{
				INT32 best = -1;
				INT32 bestValue = 0;
				for (INT32 t = 0; t < g_unitCount; ++t)
				{
					if (g_units[t].side == DEFENDER_SIDE || state.u[t].count <= 0)
						continue;
					const INT32 damage = static_cast<INT32>(TOWER_ESTIMATE_PER_CREATURE * count * g_units[t].shotDamageFactor);
					INT32 value = StockCreatureValue(t, damage, HealthLost(state, t), desperate, StockFightValue(state, t, true));
					value /= !g_units[t].disabled && threat[t] > 0 && threat[t] < STOCK_SHOT_TURN_LIMIT ? threat[t] : STOCK_SHOT_FAR_DIVISOR;
					if (bestValue <= value)
					{
						best = t;
						bestValue = value;
					}
				}
				if (pass == 0 && desperate && (best < 0 || bestValue < 1))
				{
					desperate = false;
					continue;
				}
				return best;
			}
			return -1;
		}

		void TowerShots(State& state, Outcome& outcome)
		{
			for (INT32 i = 0; i < g_towers; ++i)
			{
				if (state.fort[g_towerTarget[i]] <= 0)
					continue;
				state.lastSide = DEFENDER_SIDE;
				const INT32 t = TowerTarget(state, g_towerCount[i]);
				if (t < 0)
					continue;
				const INT32 before = state.u[t].count;
				Hit(state, t, TOWER_MAX_PER_CREATURE * g_towerCount[i], outcome);
				Trace("\n      sim r%d tower x%d | %s x%d->x%d", state.round, g_towerCount[i], g_units[t].stack->GetCreatureName(), before, state.u[t].count);
			}
		}

		bool CatapultValid(const State& state, INT32 target)
		{
			if ((target == CATAPULT_UPPER_TOWER || target == CATAPULT_LOWER_TOWER) && g_fortLevel < FORT_CASTLE)
				return false;
			if (target == CATAPULT_KEEP && g_fortLevel < FORT_CITADEL)
				return false;
			return state.fort[target] > 0;
		}

		bool IsCatapultWall(INT32 target)
		{
			for (INT32 wall : CATAPULT_WALLS)
				if (wall == target)
					return true;
			return false;
		}

		INT32 CatapultAim(const State& state)
		{
			bool any = false;
			for (INT32 k = 0; k < CATAPULT_TARGET_COUNT && !any; ++k)
				any = CatapultValid(state, k);
			if (!any)
				return -1;
			if (g_ballistics > 0 && state.fort[CATAPULT_GATE] > 0)
				return CATAPULT_GATE;
			INT32 standing = 0;
			INT32 lowest = 0;
			INT32 aim = -1;
			for (INT32 wall : CATAPULT_WALLS)
			{
				if (state.fort[wall] <= 0)
					continue;
				++standing;
				if (aim < 0 || state.fort[wall] < lowest)
				{
					aim = wall;
					lowest = state.fort[wall];
				}
			}
			if (standing >= 1 && (g_ballistics == 0 || standing == BREACHES))
				return aim;
			for (INT32 k : CATAPULT_FALLBACK)
				if (CatapultValid(state, k))
					return k;
			for (INT32 k = 0; k < CATAPULT_TARGET_COUNT; ++k)
				if (CatapultValid(state, k))
					return k;
			return -1;
		}

		void FortDamage(State& state, INT32 target, INT32 damage)
		{
			state.fort[target] = state.fort[target] > damage ? state.fort[target] - damage : 0;
		}

		void CatapultShots(State& state)
		{
			if (!g_catapult)
				return;
			state.lastSide = DEFENDER_SIDE;
			INT32 target = CatapultAim(state);
			if (target < 0)
				return;
			const INT32 noDamage = g_ballisticsRow[BALLISTICS_NO_DAMAGE];
			const INT32 onePoint = g_ballisticsRow[BALLISTICS_ONE_POINT];
			const INT32 expected = (onePoint + 2 * (100 - noDamage - onePoint)) * FORT_HP_SCALE / 100;
			for (INT32 shot = 0; shot < g_ballisticsRow[BALLISTICS_SHOTS]; ++shot)
			{
				const INT32 before = state.fort[target];
				if (IsCatapultWall(target))
					FortDamage(state, target, expected);
				else
				{
					const INT32 chance = target == CATAPULT_UPPER_TOWER || target == CATAPULT_LOWER_TOWER ? g_ballisticsRow[1]
						: target == CATAPULT_GATE ? g_ballisticsRow[2] : target == CATAPULT_KEEP ? g_ballisticsRow[0] : g_ballisticsRow[3];
					INT32 nearest = -1;
					INT32 ties = 0;
					for (INT32 wall : CATAPULT_MISS_WALLS)
					{
						if (wall == target || !CatapultValid(state, wall))
							continue;
						const INT32 d = wall > target ? wall - target : target - wall;
						if (nearest < 0 || d < nearest)
						{
							nearest = d;
							ties = 1;
						}
						else if (d == nearest)
							++ties;
					}
					const INT32 missed = ties > 0 ? expected * (100 - chance) / 100 : 0;
					for (INT32 wall : CATAPULT_MISS_WALLS)
					{
						if (wall == target || !CatapultValid(state, wall))
							continue;
						if ((wall > target ? wall - target : target - wall) == nearest)
							FortDamage(state, wall, missed / ties);
					}
					FortDamage(state, target, expected - missed);
				}
				Trace("\n      sim r%d catapult %d hp %.1f->%.1f", state.round,
					*reinterpret_cast<const INT16*>(CATAPULT_TARGETS + target * CATAPULT_TARGET_SIZE), before / static_cast<double>(FORT_HP_SCALE),
					state.fort[target] / static_cast<double>(FORT_HP_SCALE));
				if (CatapultValid(state, target))
					continue;
				INT32 next = -1;
				for (INT32 k = 0; k < CATAPULT_TARGET_COUNT; ++k)
					if (k != target && CatapultValid(state, k) && (next < 0 || (k > target ? k - target : target - k) < (next > target ? next - target : target - next)))
						next = k;
				if (next < 0)
					return;
				target = next;
			}
		}

		INT32 Rollout(State& state, Outcome& outcome, bool stopAtEnemy = false)
		{
			for (INT32 step = 0; step < MAX_ROLLOUT_ACTIONS; ++step)
			{
				if (!SideAlive(state, g_side) || !SideAlive(state, 1 - g_side))
					return -1;
				++g_work;
				const INT32 actor = NextActor(state);
				if (actor < 0)
				{
					if (state.round + 1 >= g_startRound + ROLLOUT_ROUNDS)
						return -1;
					++state.round;
					state.waitPhase = false;
					for (INT32 u = 0; u < g_unitCount; ++u)
					{
						state.u[u].done = false;
						state.u[u].waited = false;
						state.u[u].retaliations = g_units[u].maxRetaliations;
					}
					TowerShots(state, outcome);
					CatapultShots(state);
					continue;
				}
				if (stopAtEnemy && !Ours(actor))
					return actor;
				const State before = state;
				if (!Ours(actor))
					EnemyAct(state, actor, outcome);
				else if (!GuardAct(state, actor, outcome))
				{
					if (g_refineActive && !g_searching && state.round <= g_refineUntilRound)
						SearchAct(state, actor, outcome);
					else
						AllyAct(state, actor, outcome);
				}
				TraceAction(before, state, actor);
			}
			return -1;
		}

		void ApplyCandidate(State& state, INT32 a, const Candidate& candidate, Outcome& outcome)
		{
			state.u[a].defending = false;
			switch (candidate.kind)
			{
			case Kind::Wait:
				state.u[a].waited = true;
				state.lastSide = g_units[a].side;
				MoatHit(state, a, outcome);
				break;
			case Kind::Walk:
				MoveTo(state, a, candidate.hex, outcome);
				FinishTurn(state, a);
				break;
			case Kind::Attack:
			{
				const bool inPlace = candidate.hex == state.u[a].head;
				MoveTo(state, a, candidate.hex, outcome);
				Exchange(state, a, candidate.target, outcome);
				if (inPlace)
					MoatHit(state, a, outcome);
				FinishTurn(state, a);
				break;
			}
			case Kind::Shoot:
				Shoot(state, a, candidate.target, outcome);
				MoatHit(state, a, outcome);
				FinishTurn(state, a);
				break;
			default:
				state.u[a].defending = true;
				MoatHit(state, a, outcome);
				FinishTurn(state, a);
				break;
			}
		}

		INT32 AllyCandidates(const State& state, INT32 a, Candidate* out, INT32 capacity)
		{
			INT32 count = 0;
			auto add = [&](Kind kind, INT32 hex, INT32 target)
			{
				if (count < capacity)
					out[count++] = { kind, hex, target };
			};
			add(Kind::Defend, -1, -1);
			if (!state.waitPhase && !state.u[a].waited)
				add(Kind::Wait, -1, -1);
			const bool canShoot = CanShootNow(state, a);
			Occupancy occupant;
			BuildOccupancy(state, occupant);
			INT32 distance[HEX_COUNT];
			Reach(a, state, occupant, g_units[a].speed, distance);
			for (INT32 t = 0; t < g_unitCount; ++t)
				if (canShoot && g_units[t].side != g_units[a].side && state.u[t].count > 0)
					add(Kind::Shoot, -1, t);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (distance[hex] < 0)
					continue;
				if (hex != state.u[a].head)
					add(Kind::Walk, hex, -1);
				if (hex != state.u[a].head && MoatStop(state, a, hex))
					continue;
				for (INT32 t = 0; t < g_unitCount; ++t)
					if (g_units[t].side != g_units[a].side && state.u[t].count > 0 && Adjacent(a, hex, t, state.u[t].head))
						add(Kind::Attack, hex, t);
			}
			return count;
		}

		double LeafScore(const Outcome& outcome, const State& state)
		{
			return Net(outcome) - EXPOSURE_WEIGHT * LOSS_WEIGHT * Exposure(state);
		}

		double ScoreCandidate(const State& state, INT32 a, const Candidate& candidate)
		{
			State trial = state;
			Outcome o;
			ApplyCandidate(trial, a, candidate, o);
			Rollout(trial, o);
			return LeafScore(o, trial) + (candidate.kind == Kind::Wait ? WAIT_BONUS : 0.0);
		}

		void SearchAct(State& state, INT32 a, Outcome& outcome)
		{
			Candidate* candidates = Scratch().candidates;
			const INT32 count = AllyCandidates(state, a, candidates, MAX_CANDIDATES);
			const PCHAR savedTrace = g_trace;
			g_trace = nullptr;
			g_searching = true;
			double scores[MAX_CANDIDATES];
			if (g_task >= 0)
				ScoreInPool(state, a, candidates, count, scores);
			else
				for (INT32 i = 0; i < count; ++i)
					scores[i] = ScoreCandidate(state, a, candidates[i]);
			INT32 best = 0;
			for (INT32 i = 1; i < count; ++i)
				if (scores[i] > scores[best])
					best = i;
			g_searching = false;
			g_trace = savedTrace;
			ApplyCandidate(state, a, candidates[best], outcome);
		}

		void FindPaths(H3CombatMonster& stack, INT32 side, INT32 speed, INT32 maxDistance)
		{
			void* pathfinder = *reinterpret_cast<void**>(PATHFINDER);
			THISCALL_6(VOID, FIND_PATHS, pathfinder, &stack, side, speed, 0, maxDistance);
		}

		INT32 PathDistance(INT32 hex, INT32 maxDistance)
		{
			BYTE* pathfinder = *reinterpret_cast<BYTE**>(PATHFINDER);
			BYTE* nodes = pathfinder ? *reinterpret_cast<BYTE**>(pathfinder + PATH_NODES) : nullptr;
			if (!nodes)
				return -1;
			const BYTE* node = nodes + hex * PATH_NODE_SIZE;
			const DWORD flags = *reinterpret_cast<const DWORD*>(node + PATH_NODE_FLAGS);
			const INT32 distance = *reinterpret_cast<const WORD*>(node + PATH_NODE_DISTANCE);
			if (!(flags & 1) || (flags & 0xFC000000) || distance > maxDistance)
				return -1;
			return distance;
		}

		void BaseDamage(H3CombatMonster& stack, INT32& low, INT32& high)
		{
			if (stack.activeSpellsDuration[NH3Spells::BLESS] || stack.activeSpellsDuration[NH3Spells::CURSE])
			{
				low = high = THISCALL_2(INT32, FIXED_BASE_DAMAGE, &stack, 1);
				return;
			}
			low = stack.info.damageLow * stack.numberAlive;
			high = stack.info.damageHigh * stack.numberAlive;
		}

		INT32 Preview(H3CombatMonster& attacker, H3CombatMonster& target, INT32 base, bool ranged)
		{
			return THISCALL_7(INT32, PREVIEW_DAMAGE, &attacker, &target, base, ranged ? 1 : 0, 1, 0, nullptr);
		}

		double AttackDefenceFactor(INT32 attack, INT32 defence)
		{
			if (attack > defence)
				return 1.0 + ATTACK_BONUS_PER_POINT * (attack - defence < ATTACK_BONUS_CAP ? attack - defence : ATTACK_BONUS_CAP);
			const double cut = 1.0 - DEFENCE_CUT_PER_POINT * (defence - attack);
			return cut < DEFENCE_CUT_FLOOR ? DEFENCE_CUT_FLOOR : cut;
		}

		double MoatDefenceFactor(H3CombatMonster& attacker, H3CombatMonster& target, bool atStart, bool ranged)
		{
			const INT32 attack = THISCALL_3(INT32, STOCK_ATTACK, &attacker, &target, ranged ? 1 : 0);
			const INT32 defence = THISCALL_3(INT32, STOCK_DEFENCE, &target, &attacker, 1) + (atStart ? MOAT_DEFENCE : 0);
			return AttackDefenceFactor(attack, defence - MOAT_DEFENCE) / AttackDefenceFactor(attack, defence);
		}

		void BuildDamageTables()
		{
			for (INT32 u = 0; u < g_unitCount; ++u)
				g_units[u].moatAtStart = OnMoat(g_start, u, g_start.u[u].head);
			for (INT32 a = 0; a < g_unitCount; ++a)
			{
				H3CombatMonster& attacker = *g_units[a].stack;
				INT32 low;
				INT32 high;
				BaseDamage(attacker, low, high);
				const double rangedFactor = g_units[a].doubleAttack ? 2.0 : 1.0;
				for (INT32 t = 0; t < g_unitCount; ++t)
				{
					g_avgMelee[a][t] = g_maxMelee[a][t] = g_minMelee[a][t] = g_avgRanged[a][t] = g_maxRanged[a][t] = 0.0;
					g_moatMelee[a][t] = g_moatRanged[a][t] = 1.0;
					if (a == t)
						continue;
					H3CombatMonster& target = *g_units[t].stack;
					if (g_moat)
					{
						g_moatMelee[a][t] = MoatDefenceFactor(attacker, target, g_units[t].moatAtStart, false);
						g_moatRanged[a][t] = g_units[a].shooter ? MoatDefenceFactor(attacker, target, g_units[t].moatAtStart, true) : 1.0;
					}
					const INT32 meleeLow = Preview(attacker, target, low, false);
					const INT32 meleeHigh = Preview(attacker, target, high, false);
					g_avgMelee[a][t] = (meleeLow + meleeHigh) / 2.0;
					g_maxMelee[a][t] = meleeHigh;
					g_minMelee[a][t] = meleeLow;
					if (g_units[a].shooter)
					{
						const INT32 rangedLow = Preview(attacker, target, low, true);
						const INT32 rangedHigh = Preview(attacker, target, high, true);
						const double distanceFactor = FarShot(a, g_start.u[a].head, t, g_start.u[t].head) ? 2.0 : 1.0;
						g_avgRanged[a][t] = (rangedLow + rangedHigh) / 2.0 * rangedFactor * distanceFactor;
						g_maxRanged[a][t] = rangedHigh * rangedFactor * distanceFactor;
						if (WallShot(g_start, a, g_start.u[a].head, t, g_start.u[t].head))
						{
							g_avgRanged[a][t] *= 2.0;
							g_maxRanged[a][t] *= 2.0;
						}
					}
				}
			}
		}

		bool Snapshot(H3CombatManager& combat, INT32 side, H3CombatMonster& actor)
		{
			g_combat = &combat;
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				const H3AdjacentSquares& adjacent = combat.adjacentSquares[hex];
				g_adjacent[hex][0] = adjacent.topRight;
				g_adjacent[hex][1] = adjacent.right;
				g_adjacent[hex][2] = adjacent.bottomRight;
				g_adjacent[hex][3] = adjacent.bottomLeft;
				g_adjacent[hex][4] = adjacent.left;
				g_adjacent[hex][5] = adjacent.topLeft;
			}
			g_side = side;
			++g_cacheGeneration;
			g_unitCount = 0;
			g_actor = -1;
			g_startRound = combat.turn;
			g_fortLevel = combat.town ? combat.siegeKind2 : 0;
			g_siege = g_fortLevel > 0;
			g_gate = g_siege ? combat.siegeKind : 0;
			g_gateCorpse = g_siege && (combat.squares[GATE_MOAT_HEX].deadStacksNumber != 0 || combat.squares[GATE_INNER_HEX].deadStacksNumber != 0);
			g_gateInnerHolds = g_siege && combat.town->type == FORTRESS_TOWN;
			g_gateHeld = g_siege && (combat.squares[GATE_HEX].deadStacksNumber != 0 || combat.squares[GATE_MOAT_HEX].deadStacksNumber != 0
				|| (g_gateInnerHolds && combat.squares[GATE_INNER_HEX].deadStacksNumber != 0));
			for (INT32 k = 0; k < SIEGE_ROWS; ++k)
				g_wallBlocked[k] = g_siege && (combat.squares[WALL_HEX[k]].obstacleBits & SQUARE_OBSTACLE) != 0;
			for (INT32 e = 0; e < FORT_ELEMENTS; ++e)
				g_wallHp[e] = g_siege ? combat.fortWallsHp[e] : 0;
			for (INT32 k = 0; k < CATAPULT_TARGET_COUNT; ++k)
			{
				const INT32 element = *reinterpret_cast<const INT32*>(CATAPULT_TARGETS + k * CATAPULT_TARGET_SIZE + CATAPULT_TARGET_ELEMENT);
				g_start.fort[k] = g_siege ? combat.fortWallsHp[element] * FORT_HP_SCALE : 0;
			}
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				g_breachSlot[hex] = -1;
			for (INT32 k = 0; k < BREACHES; ++k)
			{
				const INT16 row = *reinterpret_cast<const INT16*>(CATAPULT_TARGETS + CATAPULT_WALLS[k] * CATAPULT_TARGET_SIZE + CATAPULT_TARGET_ROW);
				g_breachHex[k] = *reinterpret_cast<const BYTE*>(WALL_HEX_TABLE + row);
				if (g_siege)
					g_breachSlot[g_breachHex[k]] = static_cast<INT8>(k);
			}
			g_catapult = false;
			g_ballistics = 0;
			if (g_siege && side != DEFENDER_SIDE)
			{
				for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
				{
					const H3CombatMonster& stack = combat.stacks[side][i];
					if (stack.type == NH3Creatures::CATAPULT && stack.numberAlive > 0)
						g_catapult = true;
				}
				const H3Hero* hero = combat.hero[side];
				g_ballistics = hero ? reinterpret_cast<const INT8*>(hero)[HERO_BALLISTICS] : 0;
				memcpy(g_ballisticsRow, *reinterpret_cast<const INT8* const*>(BALLISTICS_TABLE) + g_ballistics * BALLISTICS_ROW_SIZE, sizeof(g_ballisticsRow));
			}
			g_moat = g_siege && (combat.hasMoat & 0xFF) != 0;
			g_moatTable = g_moat ? reinterpret_cast<const INT32*>(MOAT_DAMAGE_TABLE)[combat.town->type] : 0;
			g_moatDamage = H3Internal::_main()->mapKind >= SOD_MAP_KIND ? g_moatTable : 0;
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				g_moatHex[hex] = false;
			for (INT32 k = 0; g_moat && k < SIEGE_ROWS; ++k)
			{
				g_moatHex[reinterpret_cast<const BYTE*>(MOAT_FIRST_LINE)[k]] = true;
				if ((combat.hasMoat >> 8) & 0xFF)
					g_moatHex[reinterpret_cast<const BYTE*>(MOAT_SECOND_LINE)[k]] = true;
			}
			g_towers = 0;
			for (INT32 i = 0; g_siege && side != DEFENDER_SIDE && i < STACKS_PER_SIDE && g_towers < TOWERS; ++i)
			{
				const H3CombatMonster& tower = combat.stacks[DEFENDER_SIDE][i];
				if (tower.type == NH3Creatures::ARROW_TOWER && tower.numberAlive > 0 && !tower.info.flags.cannotMove)
				{
					g_towerTarget[g_towers] = tower.position == UPPER_TOWER_HEX ? CATAPULT_UPPER_TOWER : tower.position == LOWER_TOWER_HEX ? CATAPULT_LOWER_TOWER : CATAPULT_KEEP;
					g_towerCount[g_towers++] = tower.numberAlive;
				}
			}
			const INT32 difficulty = H3Internal::_main()->playersInfo.difficulty;
			const bool enemyIsHuman = combat.isNotAI[1 - side] != 0;
			g_stockCareful = difficulty > 0 || enemyIsHuman;
			g_enemyCanWait = difficulty > 1 || enemyIsHuman;
			const bool waitPhase = combat.waitPhase != 0;
			INT32 stockContext[16] = {};
			THISCALL_3(VOID, STOCK_AI_CONTEXT, stockContext, &combat, 1 - side);
			for (INT32 s = 0; s < 2; ++s)
			{
				for (INT32 i = 0; i < STACKS_PER_SIDE; ++i)
				{
					H3CombatMonster& stack = combat.stacks[s][i];
					if (!IsFighter(stack))
						continue;
					const INT32 u = g_unitCount++;
					Unit& unit = g_units[u];
					unit.stack = &stack;
					unit.side = stack.side;
					unit.sideIndex = stack.sideIndex;
					unit.maxHp = stack.info.hitPoints > 0 ? stack.info.hitPoints : 1;
					unit.speed = stack.GetStackSpeed();
					unit.wide = stack.info.flags.doubleWide != 0;
					unit.tailOffset = stack.secondHexOrientation != 0 ? 1 : -1;
					unit.facing = stack.secondHexOrientation;
					unit.maxRetaliations = stack.retaliations > 1 ? stack.retaliations : 1;
					unit.morale = stack.spellsData.morale;
					unit.value = stack.info.aiValue > 0 ? stack.info.aiValue : 1;
					unit.fightValue = stack.info.fightValue > 0 ? stack.info.fightValue : 1;
					unit.fvPerCreature = THISCALL_5(double, STOCK_FIGHT_VALUE, &stack, stockContext[0], stockContext[1], 0, 0);
					unit.flyer = stack.info.flags.flyer != 0;
					unit.shooter = stack.info.flags.shooter && stack.info.numberShots > 0;
					unit.fvRanged = unit.shooter ? THISCALL_5(double, STOCK_FIGHT_VALUE, &stack, stockContext[0], stockContext[1], 1, 0) : unit.fvPerCreature;
					unit.shotDamageFactor = THISCALL_2(double, STOCK_SHOT_DAMAGE_FACTOR, &stack, 1);
					unit.noRetaliation = stack.info.flags.noRetaliation != 0;
					unit.doubleAttack = stack.info.flags.doubleAttack != 0;
					unit.strikesTwice = unit.doubleAttack && !stack.info.flags.shooter;
					unit.type = stack.type;
					unit.teleporter = stack.type == NH3Creatures::DEVIL || stack.type == NH3Creatures::ARCH_DEVIL;
					unit.breath = stack.info.flags.extendedAttack != 0;
					unit.allAround = stack.info.flags.attackAllAround != 0;
					unit.threeHeaded = stack.type == NH3Creatures::CERBERUS;
					unit.deathCloud = stack.type == NH3Creatures::LICH || stack.type == NH3Creatures::POWER_LICH;
					unit.cloudShooter = unit.deathCloud || stack.type == NH3Creatures::MAGOG;
					unit.alive = stack.info.flags.alive != 0;
					unit.undead = stack.info.flags.undead != 0;
					unit.summoned = stack.info.flags.summon != 0;
					unit.harpy = stack.type == NH3Creatures::HARPY || stack.type == NH3Creatures::HARPY_HAG;
					unit.petrified = stack.activeSpellsDuration[NH3Spells::STONE] != 0;
					unit.disabled = unit.petrified || stack.activeSpellsDuration[NH3Spells::BLIND] != 0 || stack.activeSpellsDuration[NH3Spells::PARALYZE] != 0;
					unit.threatens = stack.activeSpellsDuration[NH3Spells::STONE] < 2 && stack.activeSpellsDuration[NH3Spells::BLIND] < 2
						&& stack.activeSpellsDuration[NH3Spells::BERSERK] == 0 && stack.activeSpellsDuration[NH3Spells::HYPNOTIZE] == 0;
					unit.fireball = stack.info.flags.fireballAttack != 0;
					unit.noMeleePenalty = stack.info.flags.noMeleePenalty != 0;
					H3Hero* hero = combat.hero[stack.side];
					unit.rangePenalty = stack.type != NH3Creatures::SHARPSHOOTER && stack.type != NH3Creatures::ARROW_TOWER
						&& !(hero && (hero->WearsArtifact(NH3Artifacts::GOLDEN_BOW) || hero->WearsArtifact(NH3Artifacts::BOW_OF_THE_SHARPSHOOTER)));
						const INT32 controller = stack.activeSpellsDuration[NH3Spells::HYPNOTIZE] ? 1 - stack.side : stack.side;
						H3Hero* shotHero = combat.hero[controller];
						unit.wallExempt = stack.type == NH3Creatures::MAGE || stack.type == NH3Creatures::ARCH_MAGE || stack.type == NH3Creatures::ENCHANTER
							|| stack.type == NH3Creatures::SHARPSHOOTER || stack.type == NH3Creatures::ARROW_TOWER
							|| (shotHero && (shotHero->WearsArtifact(NH3Artifacts::GOLDEN_BOW) || shotHero->WearsArtifact(NH3Artifacts::BOW_OF_THE_SHARPSHOOTER)));
					unit.heroBlast = stack.side != side && g_stockCareful && THISCALL_5(INT32, STOCK_AREA_THREAT, &combat, side, &stack, 0, stockContext) != 0;
					unit.moatWalker = !unit.flyer && !unit.teleporter;

					UnitState& state = g_start.u[u];
					state.count = stack.numberAlive;
					state.topHp = unit.maxHp - stack.healthLost;
					if (state.topHp <= 0)
						state.topHp = unit.maxHp;
					state.retaliations = stack.retaliations;
					state.head = stack.position;
					state.done = stack.info.flags.done != 0;
					state.waited = waitPhase ? !state.done : stack.info.flags.waiting != 0;
					state.defending = stack.info.flags.defending != 0;
					if (&stack == &actor)
						g_actor = u;
				}
			}
			if (g_actor < 0)
				return false;
			for (INT32 a = 0; a < g_unitCount; ++a)
			{
				for (INT32 t = 0; t < g_unitCount; ++t)
				{
					g_stockDamage[a][t].assign(g_units[a].stack->numberAlive + 1, -1);
					g_stockRangedDamage[a][t].assign(g_units[a].shooter ? g_units[a].stack->numberAlive + 1 : 1, -1);
				}
			}
			g_strongerAtRange = g_stockCareful && THISCALL_2(BOOL8, STOCK_STRONGER_AT_RANGE, &combat, stockContext) != 0;
			g_stayInside = false;
			if (g_siege && side != DEFENDER_SIDE)
			{
				INT32 i = 0;
				for (INT32 element : STAY_INSIDE_ELEMENTS)
				{
					const INT16 row = *reinterpret_cast<const INT16*>(CATAPULT_TARGETS + element * CATAPULT_TARGET_SIZE + CATAPULT_TARGET_ROW);
					const INT32 hex = row == -1 ? -1 : *reinterpret_cast<const BYTE*>(WALL_HEX_TABLE + row);
					g_stayHexBlocked[i] = THISCALL_2(BOOL8, STOCK_SIEGE_BLOCKED, &combat, hex) != 0;
					g_stayTarget[i] = -1;
					for (INT32 k = 0; k < CATAPULT_TARGET_COUNT; ++k)
						if (*reinterpret_cast<const INT32*>(CATAPULT_TARGETS + k * CATAPULT_TARGET_SIZE + CATAPULT_TARGET_ELEMENT) == element)
							g_stayTarget[i] = k;
					++i;
				}
				g_stayInside = g_stockCareful ? g_strongerAtRange : THISCALL_2(BOOL8, STOCK_STRONGER_AT_RANGE, &combat, stockContext) != 0;
			}
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				const H3CombatSquare& square = combat.squares[hex];
				g_obstacle[hex] = (square.obstacleBits & SQUARE_OBSTACLE) != 0;
				if (square.stackSide >= 0 && square.stackSide < 2 && square.stackIndex >= 0 && square.stackIndex < STACKS_PER_SIDE
					&& !IsFighter(combat.stacks[square.stackSide][square.stackIndex]))
					g_obstacle[hex] = true;
				const INT32 column = hex % BATTLE_COLUMNS;
				g_stockOpen[hex] = column != 0 && column != BATTLE_COLUMNS - 1 && !g_obstacle[hex];
			}
			for (INT32 s = 0; s < 2; ++s)
			{
				memset(g_deadStackHex[s], 0, sizeof(g_deadStackHex[s]));
				for (INT32 i = 0; i < combat.heroMonCount[s] && i < STACKS_PER_SIDE; ++i)
				{
					const H3CombatMonster& stack = combat.stacks[s][i];
					if (stack.numberAlive <= 0 && stack.type != NH3Creatures::ARROW_TOWER && stack.position >= 0 && stack.position < HEX_COUNT)
						g_deadStackHex[s][stack.position] = true;
				}
			}
			g_start.lastSide = 1 - side;
			g_start.round = combat.turn;
			g_start.waitPhase = waitPhase;

			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				g_standable[hex] = false;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				INT32 hexes[2];
				const INT32 count = HexesAt(u, g_start.u[u].head, hexes);
				for (INT32 i = 0; i < count; ++i)
					g_standable[hexes[i]] = true;
				FindPaths(*g_units[u].stack, g_units[u].side, FULL_MAP_DISTANCE, FULL_MAP_DISTANCE);
				for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
				{
					if (PathDistance(hex, FULL_MAP_DISTANCE) < 0)
						continue;
					const INT32 standCount = HexesAt(u, hex, hexes);
					for (INT32 i = 0; i < standCount; ++i)
						g_standable[hexes[i]] = true;
				}
			}
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				const INT32 column = hex % BATTLE_COLUMNS;
				g_headOk[hex] = g_standable[hex] && column != 0 && column != BATTLE_COLUMNS - 1;
			}
			if (g_siege)
				g_headOk[GATE_HEX] = g_headOk[GATE_MOAT_HEX] = g_gate != GATE_CLOSED || g_side == DEFENDER_SIDE;

			FindPaths(actor, g_units[g_actor].side, g_units[g_actor].speed, -1);
			Occupancy occupant;
			BuildOccupancy(g_start, occupant);
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				g_actorDistance[hex] = -1;
				if (hex == actor.position)
					g_actorDistance[hex] = 0;
				else if (PathDistance(hex, g_units[g_actor].speed) >= 0 && CanStand(g_actor, hex, occupant))
					g_actorDistance[hex] = PathDistance(hex, g_units[g_actor].speed);
			}

			BuildDamageTables();
			return true;
		}

		double Worth(const State& state, INT32 u)
		{
			return g_units[u].value * state.u[u].count;
		}

		bool MakeGuardPlan(const State& state, INT32 p, GuardPlan& plan)
		{
			for (INT32 u = 0; u < MAX_UNITS; ++u)
				plan.guarded[u] = plan.mover[u] = false;
			INT32 bigStacks = 0;
			for (INT32 u = 0; u < g_unitCount; ++u)
				if (Ours(u) && state.u[u].count > 1)
					++bigStacks;
			if (p < 0)
			{
				if (bigStacks < 2)
					return false;
				for (INT32 u = 0; u < g_unitCount; ++u)
					plan.guarded[u] = Ours(u) && state.u[u].count > 1;
			}
			else if (Ours(p) && state.u[p].count > 1)
				plan.guarded[p] = true;
			else
				return false;

			double cheapestGuarded = 0.0;
			bool any = false;
			for (INT32 u = 0; u < g_unitCount; ++u)
				if (plan.guarded[u] && (!any || Worth(state, u) < cheapestGuarded))
				{
					cheapestGuarded = Worth(state, u);
					any = true;
				}

			Occupancy occupant;
			BuildOccupancy(state, occupant);
			bool open[HEX_COUNT];
			bool wall[HEX_COUNT];
			if (!GuardHexes(state, plan, occupant, open, wall))
				return false;
			INT32 openCount = 0;
			INT32 matched[HEX_COUNT];
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				matched[hex] = -1;
				if (open[hex])
					++openCount;
			}
			if (openCount == 0)
				return false;

			static INT32 reach[MAX_UNITS][HEX_COUNT];
			INT32 order[MAX_UNITS];
			INT32 orderCount = 0;
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				if (!Ours(u) || state.u[u].count <= 0 || state.u[u].done || plan.guarded[u] || Worth(state, u) >= cheapestGuarded)
					continue;
				Reach(u, state, occupant, g_units[u].speed, reach[u]);
				order[orderCount++] = u;
			}
			std::stable_sort(order, order + orderCount, [&state](INT32 a, INT32 b) { return Worth(state, a) < Worth(state, b); });
			INT32 matchedCount = 0;
			for (INT32 i = 0; i < orderCount && matchedCount < openCount; ++i)
			{
				bool seen[HEX_COUNT] = {};
				if (Augment(order[i], reach, open, matched, seen))
					++matchedCount;
			}
			if (matchedCount < openCount)
				return false;
			for (INT32 hex = 0; hex < HEX_COUNT; ++hex)
			{
				if (matched[hex] >= 0)
					plan.mover[matched[hex]] = true;
				else if (wall[hex] && occupant[hex] >= 0)
					plan.mover[occupant[hex]] = true;
			}
			return true;
		}

		INT32 AddGuardPlan(const State& state, INT32 p)
		{
			if (g_guardPlanCount >= MAX_GUARD_PLANS || !MakeGuardPlan(state, p, g_guardPlans[g_guardPlanCount]))
				return -1;
			return g_guardPlanCount++;
		}

		INT32 DistanceToEnemies(const State& state, INT32 u, INT32 head, bool shootersOnly = false)
		{
			INT32 goal[HEX_COUNT];
			ApproachDistances(state, 1 - g_side, goal, shootersOnly);
			INT32 hexes[2];
			const INT32 count = HexesAt(u, head, hexes);
			INT32 best = FAR_AWAY;
			for (INT32 i = 0; i < count; ++i)
				if (goal[hexes[i]] >= 0 && goal[hexes[i]] < best)
					best = goal[hexes[i]];
			return best;
		}

		Decision Evaluate(const Candidate& candidate, const Context& context)
		{
			State state = g_start;
			Outcome outcome;
			double bonus = 0.0;
			const INT32 start = state.u[g_actor].head;
			g_guard = candidate.guard >= 0 ? &g_guardPlans[candidate.guard] : nullptr;
			ApplyCandidate(state, g_actor, candidate, outcome);
			if (candidate.kind == Kind::Wait)
				bonus += WAIT_BONUS;

			const INT32 moved = state.u[g_actor].head;
			if (moved != start)
			{
				bonus -= MOVE_COST_PER_HEX * g_actorDistance[moved];
				if (!g_units[g_actor].shooter)
					bonus += SHOOTER_APPROACH_PER_HEX * (DistanceToEnemies(g_start, g_actor, start, true) - DistanceToEnemies(g_start, g_actor, moved, true));
				if (context.stalledRounds > 0)
				{
					const INT32 closer = DistanceToEnemies(g_start, g_actor, start) - DistanceToEnemies(g_start, g_actor, moved);
					bonus += APPROACH_BONUS_PER_HEX * context.stalledRounds * closer * g_units[g_actor].value * g_start.u[g_actor].count;
				}
			}

			Decision decision;
			decision.kind = candidate.kind;
			decision.hex = candidate.hex;
			decision.target = candidate.target >= 0 ? g_units[candidate.target].stack : nullptr;

			if (g_robust)
			{
				const INT32 e = Rollout(state, outcome, true);
				EnemyReplies replies;
				if (e >= 0)
					PlanEnemy(state, e, nullptr, 0, &replies);
				if (replies.count > 1)
				{
					double scores[ROBUST_REPLIES];
					Outcome outcomes[ROBUST_REPLIES];
					INT32 order[ROBUST_REPLIES];
					const PCHAR savedTrace = g_trace;
					for (INT32 k = 0; k < replies.count; ++k)
					{
						State branch = state;
						Outcome o = outcome;
						EnemyActPlan(branch, e, replies.plans[k], o);
						TraceAction(state, branch, e);
						Rollout(branch, o);
						scores[k] = LeafScore(o, branch);
						outcomes[k] = o;
						order[k] = k;
						g_trace = nullptr;
					}
					g_trace = savedTrace;
					g_guard = nullptr;
					std::sort(order, order + replies.count, [&scores](INT32 a, INT32 b) { return scores[a] < scores[b]; });
					double weightSum = 0.0;
					for (INT32 k = 0; k < replies.count; ++k)
						weightSum += ROBUST_WEIGHTS[k];
					decision.score = bonus;
					decision.gained = 0.0;
					decision.lost = 0.0;
					for (INT32 k = 0; k < replies.count; ++k)
					{
						const double w = ROBUST_WEIGHTS[k] / weightSum;
						decision.score += w * scores[order[k]];
						decision.gained += w * outcomes[order[k]].gained;
						decision.lost += w * outcomes[order[k]].lost;
					}
					return decision;
				}
			}

			Rollout(state, outcome);
			g_guard = nullptr;
			bonus -= EXPOSURE_WEIGHT * LOSS_WEIGHT * Exposure(state);
			decision.gained = outcome.gained;
			decision.lost = outcome.lost;
			decision.score = Net(outcome) + bonus;
			return decision;
		}

		constexpr UINT FPU_CONTROL_MASK = _MCW_EM | _MCW_RC | _MCW_PC | _MCW_IC | _MCW_DN;

		struct TaskCancelled
		{
		};

		struct SearchTask
		{
			Candidate candidate;
			Decision decision;
			INT64 work = 0;
		};

		struct DamageRequest
		{
			INT32 attacker;
			INT32 target;
			INT32 count;
			bool ranged;
			bool pending;
			bool cancelled;
			INT32 damage;
			INT32 task;
		};

		struct SearchJob
		{
			const State* state;
			const Candidate* candidates;
			double* scores;
			INT32 actor;
			INT32 count;
			INT32 task;
			const GuardPlan* guard;
			bool inMoraleAction;
			INT32 next;
			INT32 done;
			INT64 work;
			bool cancelled;
		};

		constexpr INT32 MAX_WORKERS = 64;

		struct SearchPool
		{
			std::mutex mutex;
			std::condition_variable wake;
			std::condition_variable progress;
			SearchTask* tasks = nullptr;
			const Context* context = nullptr;
			INT32 next = 0;
			INT32 runUntil = 0;
			bool traced = false;
			UINT x87 = 0;
			UINT sse = 0;
			INT32 workers = 0;
			INT32 jobCount = 0;
			SearchJob* jobs[MAX_WORKERS];
			bool finished[MAX_CANDIDATES];
			DamageRequest requests[MAX_WORKERS];
		};

		SearchPool* g_pool = nullptr;
		thread_local INT32 g_worker = -1;

		INT32 TaskStockDamage(INT32 a, INT32 t, INT32 count, bool ranged)
		{
			ThreadScratch& scratch = Scratch();
			std::vector<INT32>& local = (ranged ? scratch.rangedDamage : scratch.damage)[a][t];
			if (local[count] >= 0)
				return local[count];
			SearchPool& pool = *g_pool;
			std::unique_lock<std::mutex> lock(pool.mutex);
			const INT32 known = (ranged ? g_stockRangedDamage : g_stockDamage)[a][t][count];
			if (known >= 0)
				return local[count] = known;
			if (g_task >= pool.runUntil)
				throw TaskCancelled();
			DamageRequest& request = pool.requests[g_worker];
			request = { a, t, count, ranged, true, false, -1, g_task };
			pool.progress.notify_one();
			pool.wake.wait(lock, [&request] { return !request.pending; });
			if (request.cancelled)
				throw TaskCancelled();
			return local[count] = request.damage;
		}

		void PrepareThread(const SearchPool& pool)
		{
			UINT ignored;
			__control87_2(pool.x87, FPU_CONTROL_MASK, &ignored, nullptr);
			__control87_2(pool.sse, FPU_CONTROL_MASK, nullptr, &ignored);
			ThreadScratch& scratch = Scratch();
			if (scratch.damageGeneration != g_cacheGeneration)
			{
				for (INT32 a = 0; a < g_unitCount; ++a)
				{
					for (INT32 t = 0; t < g_unitCount; ++t)
					{
						scratch.damage[a][t].assign(g_stockDamage[a][t].size(), -1);
						scratch.rangedDamage[a][t].assign(g_stockRangedDamage[a][t].size(), -1);
					}
				}
				scratch.damageGeneration = g_cacheGeneration;
			}
		}

		void RunTask(SearchPool& pool, INT32 index)
		{
			PrepareThread(pool);
			SearchTask& task = pool.tasks[index];
			g_task = index;
			g_work = 0;
			g_searching = false;
			g_inMoraleAction = false;
			g_guard = nullptr;
			CHAR trace[sizeof(task.decision.trace)];
			trace[0] = 0;
			g_trace = pool.traced ? trace : nullptr;
			g_traceSize = sizeof(trace);
			try
			{
				task.decision = Evaluate(task.candidate, *pool.context);
				if (pool.traced)
					memcpy(task.decision.trace, trace, sizeof(trace));
			}
			catch (const TaskCancelled&)
			{
			}
			g_trace = nullptr;
			task.work = g_work;
			g_task = -1;
		}

		SearchJob* OpenJob(const SearchPool& pool, SearchJob* preferred)
		{
			SearchJob* best = preferred && preferred->next < preferred->count ? preferred : nullptr;
			for (INT32 j = 0; j < pool.jobCount; ++j)
				if (pool.jobs[j]->next < pool.jobs[j]->count && (!best || pool.jobs[j]->task < best->task))
					best = pool.jobs[j];
			return best;
		}

		bool RunItem(SearchPool& pool, std::unique_lock<std::mutex>& lock, SearchJob* preferred)
		{
			SearchJob* job = OpenJob(pool, preferred);
			if (!job)
				return false;
			const INT32 i = job->next++;
			bool cancelled = job->task >= pool.runUntil;
			INT64 work = 0;
			lock.unlock();
			if (!cancelled)
			{
				const INT32 savedTask = g_task;
				const bool savedSearching = g_searching;
				const PCHAR savedTrace = g_trace;
				const GuardPlan* savedGuard = g_guard;
				const bool savedInMoraleAction = g_inMoraleAction;
				const INT64 savedWork = g_work;
				PrepareThread(pool);
				g_task = job->task;
				g_searching = true;
				g_trace = nullptr;
				g_guard = job->guard;
				g_inMoraleAction = job->inMoraleAction;
				g_work = 0;
				try
				{
					job->scores[i] = ScoreCandidate(*job->state, job->actor, job->candidates[i]);
				}
				catch (const TaskCancelled&)
				{
					cancelled = true;
				}
				work = g_work;
				g_task = savedTask;
				g_searching = savedSearching;
				g_trace = savedTrace;
				g_guard = savedGuard;
				g_inMoraleAction = savedInMoraleAction;
				g_work = savedWork;
			}
			lock.lock();
			job->work += work;
			job->cancelled = job->cancelled || cancelled;
			if (++job->done == job->count)
				pool.wake.notify_all();
			return true;
		}

		void ScoreInPool(const State& state, INT32 a, const Candidate* candidates, INT32 count, double* scores)
		{
			SearchPool& pool = *g_pool;
			SearchJob job = { &state, candidates, scores, a, count, g_task, g_guard, g_inMoraleAction, 0, 0, 0, false };
			{
				std::unique_lock<std::mutex> lock(pool.mutex);
				pool.jobs[pool.jobCount++] = &job;
				pool.wake.notify_all();
				while (job.done < job.count)
					if (!RunItem(pool, lock, &job))
						pool.wake.wait(lock);
				INT32 j = 0;
				while (pool.jobs[j] != &job)
					++j;
				pool.jobs[j] = pool.jobs[--pool.jobCount];
			}
			if (job.cancelled)
				throw TaskCancelled();
			g_work += job.work;
		}

		void WorkerLoop(SearchPool* pool, INT32 worker)
		{
			g_worker = worker;
			std::unique_lock<std::mutex> lock(pool->mutex);
			for (;;)
			{
				pool->wake.wait(lock, [pool] { return pool->next < pool->runUntil || OpenJob(*pool, nullptr); });
				if (RunItem(*pool, lock, nullptr))
					continue;
				const INT32 index = pool->next++;
				lock.unlock();
				RunTask(*pool, index);
				lock.lock();
				pool->finished[index] = true;
				pool->progress.notify_one();
			}
		}

		SearchPool& Pool()
		{
			if (!g_pool)
			{
				g_pool = new SearchPool();
				const INT32 cores = static_cast<INT32>(std::thread::hardware_concurrency());
				g_pool->workers = cores < 1 ? 1 : cores > MAX_WORKERS ? MAX_WORKERS : cores;
				for (INT32 i = 0; i < g_pool->workers; ++i)
					std::thread(WorkerLoop, g_pool, i).detach();
			}
			return *g_pool;
		}

		INT32 RunBatch(SearchTask* tasks, INT32 count, const Context& context, bool traced, const std::function<bool(INT32)>& wanted)
		{
			SearchPool& pool = Pool();
			std::unique_lock<std::mutex> lock(pool.mutex);
			pool.tasks = tasks;
			pool.context = &context;
			pool.next = 0;
			pool.runUntil = count;
			pool.traced = traced;
			__control87_2(0, 0, &pool.x87, &pool.sse);
			for (INT32 i = 0; i < count; ++i)
				pool.finished[i] = false;
			for (INT32 w = 0; w < pool.workers; ++w)
				pool.requests[w].pending = false;
			pool.wake.notify_all();
			INT32 lowest = 0;
			for (;;)
			{
				while (lowest < pool.runUntil && wanted(lowest) && pool.finished[lowest])
					++lowest;
				if (lowest < pool.runUntil && !wanted(lowest))
				{
					pool.runUntil = lowest;
					for (INT32 w = 0; w < pool.workers; ++w)
					{
						if (!pool.requests[w].pending || pool.requests[w].task < lowest)
							continue;
						pool.requests[w].cancelled = true;
						pool.requests[w].pending = false;
					}
					pool.wake.notify_all();
					continue;
				}
				bool served = false;
				for (INT32 w = 0; w < pool.workers; ++w)
				{
					DamageRequest& request = pool.requests[w];
					if (!request.pending)
						continue;
					request.damage = GameStockDamage(request.attacker, request.target, request.count, request.ranged);
					request.pending = false;
					served = true;
				}
				if (served)
				{
					pool.wake.notify_all();
					continue;
				}
				if (lowest >= pool.runUntil)
				{
					bool idle = true;
					for (INT32 i = lowest; i < pool.next; ++i)
						idle = idle && pool.finished[i];
					if (idle)
						break;
				}
				pool.progress.wait(lock);
			}
			const INT32 ran = pool.runUntil;
			pool.next = 0;
			pool.runUntil = 0;
			return ran;
		}

		void Describe(const Decision& decision, PCHAR out, size_t size)
		{
			if (decision.target)
				_snprintf_s(out, size, _TRUNCATE, "%s %s@%d from %d %.1f", KindName(decision.kind),
					decision.target->GetCreatureName(), decision.target->position, decision.hex, decision.score);
			else if (decision.kind == Kind::Walk)
				_snprintf_s(out, size, _TRUNCATE, "walk %d %.1f", decision.hex, decision.score);
			else
				_snprintf_s(out, size, _TRUNCATE, "%s %.1f", KindName(decision.kind), decision.score);
		}
	}

	LPCSTR KindName(Kind kind)
	{
		switch (kind)
		{
		case Kind::Wait:
			return "wait";
		case Kind::Walk:
			return "walk";
		case Kind::Attack:
			return "attack";
		case Kind::Shoot:
			return "shoot";
		default:
			return "defend";
		}
	}

	Decision Choose(H3CombatManager& combat, INT32 side, H3CombatMonster& actor, const Context& context)
	{
		Decision best;
		if (!Snapshot(combat, side, actor))
			return best;
		g_robust = ROBUST_WEIGHTS[0] > 0.0;

		const bool canShoot = THISCALL_2(BOOL8, CAN_SHOOT, &actor, 0) != 0;
		struct Scored
		{
			Candidate candidate;
			Decision decision;
		};
		static Scored scored[MAX_CANDIDATES];
		static SearchTask tasks[MAX_CANDIDATES];
		INT32 scoredCount = 0;
		auto consider = [&](const Candidate& candidate)
		{
			if (scoredCount < MAX_CANDIDATES)
				scored[scoredCount++].candidate = candidate;
		};

		consider({ Kind::Defend, -1, -1 });
		if (!combat.waitPhase && !actor.info.flags.morale)
			consider({ Kind::Wait, -1, -1 });
		if (canShoot)
			for (INT32 t = 0; t < g_unitCount; ++t)
				if (!Ours(t))
					consider({ Kind::Shoot, -1, t });

		for (INT32 stand = 0; stand < HEX_COUNT; ++stand)
		{
			if (g_actorDistance[stand] < 0)
				continue;
			if (stand != actor.position)
				consider({ Kind::Walk, stand, -1 });
			if (stand != actor.position && MoatStop(g_start, g_actor, stand))
				continue;
			for (INT32 t = 0; t < g_unitCount; ++t)
				if (!Ours(t) && Adjacent(g_actor, stand, t, g_start.u[t].head))
					consider({ Kind::Attack, stand, t });
		}

		g_guardPlanCount = 0;
		for (INT32 p = -1; p < g_unitCount; ++p)
			AddGuardPlan(g_start, p);
		const INT32 standingPlans = g_guardPlanCount;
		if (g_start.u[g_actor].count > 1)
		{
			for (INT32 stand = 0; stand < HEX_COUNT; ++stand)
			{
				if (g_actorDistance[stand] < 0 || stand == actor.position)
					continue;
				State moved = g_start;
				moved.u[g_actor].head = stand;
				moved.u[g_actor].done = true;
				INT32 g = AddGuardPlan(moved, -1);
				if (g < 0)
					g = AddGuardPlan(moved, g_actor);
				if (g >= 0)
					consider({ Kind::Walk, stand, -1, g });
			}
		}
		for (INT32 g = 0; g < standingPlans; ++g)
		{
			const GuardPlan& plan = g_guardPlans[g];
			if (plan.guarded[g_actor])
			{
				consider({ Kind::Defend, -1, -1, g });
				if (!combat.waitPhase && !actor.info.flags.morale)
					consider({ Kind::Wait, -1, -1, g });
				continue;
			}
			if (!plan.mover[g_actor])
			{
				consider({ Kind::Defend, -1, -1, g });
				if (!combat.waitPhase && !actor.info.flags.morale)
					consider({ Kind::Wait, -1, -1, g });
				continue;
			}
			const INT32 hex = GuardPick(g_start, plan, g_actor);
			if (hex == actor.position)
				consider({ Kind::Defend, -1, -1, g });
			else if (hex >= 0 && g_actorDistance[hex] >= 0)
				consider({ Kind::Walk, hex, -1, g });
		}

		if (scoredCount == 0)
			return best;
		for (INT32 i = 0; i < scoredCount; ++i)
			tasks[i].candidate = scored[i].candidate;
		RunBatch(tasks, scoredCount, context, false, [](INT32) { return true; });
		INT64 work = 0;
		for (INT32 i = 0; i < scoredCount; ++i)
		{
			scored[i].decision = tasks[i].decision;
			work += tasks[i].work;
		}
		auto byScore = [](const Scored& a, const Scored& b) { return a.decision.score > b.decision.score; };
		std::stable_sort(scored, scored + scoredCount, byScore);

		INT32 refined = scoredCount < REFINE_CANDIDATES ? scoredCount : REFINE_CANDIDATES;
		const double cutoff = scored[refined - 1].decision.score - REFINE_TIE_TOLERANCE;
		bool covered[MAX_UNITS] = {};
		for (INT32 i = 0; i < refined; ++i)
			if (scored[i].candidate.kind == Kind::Attack || scored[i].candidate.kind == Kind::Shoot)
				covered[scored[i].candidate.target] = true;
		INT32 extraEnd = refined;
		for (INT32 j = refined; j < scoredCount; ++j)
		{
			const Candidate& candidate = scored[j].candidate;
			if ((candidate.kind != Kind::Attack && candidate.kind != Kind::Shoot) || covered[candidate.target])
				continue;
			covered[candidate.target] = true;
			std::rotate(scored + extraEnd, scored + j, scored + j + 1);
			++extraEnd;
		}
		const INT32 extras = extraEnd - refined;
		refined = extraEnd;
		const INT32 guardEnd = static_cast<INT32>(std::stable_partition(scored + refined, scored + scoredCount,
			[](const Scored& s) { return s.candidate.guard >= 0; }) - scored);
		refined = guardEnd < 2 * REFINE_CANDIDATES + extras ? guardEnd : 2 * REFINE_CANDIDATES + extras;
		g_refineActive = true;
		g_refineUntilRound = g_startRound + 1;
		INT32 tied = 0;
		while (guardEnd + tied < scoredCount && scored[guardEnd + tied].decision.score >= cutoff)
			++tied;
		for (INT32 i = 0; i < refined; ++i)
			tasks[i].candidate = scored[i].candidate;
		for (INT32 i = 0; i < tied; ++i)
			tasks[refined + i].candidate = scored[guardEnd + i].candidate;
		const INT32 required = refined;
		const INT32 ran = RunBatch(tasks, refined + tied, context, true, [&](INT32 k)
		{
			if (k < required)
				return true;
			INT64 done = work;
			INT64 refineWork = 0;
			for (INT32 i = 0; i < k; ++i)
			{
				done += tasks[i].work;
				if (i < required)
					refineWork += tasks[i].work;
			}
			const INT64 perRefine = required > 0 ? refineWork / required : 0;
			return done + perRefine <= REFINE_WORK_BUDGET;
		});
		for (INT32 i = 0; i < required; ++i)
			scored[i].decision = tasks[i].decision;
		for (INT32 next = guardEnd; next - guardEnd + required < ran; ++next)
		{
			std::swap(scored[refined], scored[next]);
			scored[refined].decision = tasks[next - guardEnd + required].decision;
			++refined;
		}
		std::stable_sort(scored, scored + refined, byScore);

		best = scored[0].decision;
		g_refineActive = false;
		g_robust = false;
		CHAR second[96] = {};
		CHAR third[96] = {};
		if (scoredCount > 1)
			Describe(scored[1].decision, second, sizeof(second));
		if (scoredCount > 2)
			Describe(scored[2].decision, third, sizeof(third));
		CHAR guard[96] = {};
		if (scored[0].candidate.guard >= 0)
		{
			const GuardPlan& plan = g_guardPlans[scored[0].candidate.guard];
			for (INT32 u = 0; u < g_unitCount; ++u)
			{
				if (!plan.guarded[u])
					continue;
				const size_t used = strlen(guard);
				_snprintf_s(guard + used, sizeof(guard) - used, _TRUNCATE, "%s%s@%d", used ? " " : "guard ", g_units[u].stack->GetCreatureName(), g_start.u[u].head);
			}
			const size_t used = strlen(guard);
			_snprintf_s(guard + used, sizeof(guard) - used, _TRUNCATE, ", ");
		}
		_snprintf_s(best.alternatives, sizeof(best.alternatives), _TRUNCATE, "%snext: %s | %s", guard, second, third);
		return best;
	}

	void PredictStockTurn(H3CombatManager& combat, H3CombatMonster& actor, PCHAR out, size_t size)
	{
		out[0] = 0;
		if (!Snapshot(combat, 1 - actor.side, actor))
			return;
		const EnemyPlan plan = PlanEnemy(g_start, g_actor, out, size);
		const size_t used = strlen(out);
		LPCSTR action = plan.shoot ? "shoot" : plan.attackNow ? "attack" : plan.step >= 0 ? "walk" : plan.wait ? "wait" : "defend";
		_snprintf_s(out + used, size - used, _TRUNCATE, "\n      port picks %s %s at %d, stand %d, step %d, hex %d%s", action,
			plan.target >= 0 ? g_units[plan.target].stack->GetCreatureName() : "-", plan.target >= 0 ? g_start.u[plan.target].head : -1,
			plan.stand, plan.step, plan.hex, plan.desperate ? ", desperate" : "");
	}
}
