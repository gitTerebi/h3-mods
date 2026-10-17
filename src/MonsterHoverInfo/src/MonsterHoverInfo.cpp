#include "MonsterHoverInfo.hpp"

#include <cmath>

using namespace h3;

MonsterHoverInfoPlugin MonsterHoverInfo;

MonsterHoverInfoPlugin& MonsterHoverInfoPlugin::GetPlugin()
{
	return MonsterHoverInfo;
}

namespace
{
	bool IsMonsterObject(INT32 type)
	{
		return type == NH3Objects::MONSTER;
	}

	constexpr INT32 HOTA_FIRST_CREATURE = 151;
	// Measured on HotA 1.8.1: entries 151-199 hold creatures, 200+ are zeroed.
	constexpr INT32 HOTA_LAST_CREATURE  = 199;

	bool IsHotA()
	{
		static const bool hota = GetModuleHandleA("HotA.dll") != nullptr;
		return hota;
	}

	bool IsCreatureId(INT32 type)
	{
		if (type >= NH3Creatures::PIKEMAN && type <= NH3Creatures::TROLL)
			return true;
		if (!IsHotA() || type < HOTA_FIRST_CREATURE || type > HOTA_LAST_CREATURE)
			return false;

		const H3CreatureInformation& creature = P_Creatures()[type];
		return creature.level >= 0 && creature.level <= 6 && creature.aiValue > 0;
	}

	void GetQuantityRange(INT32 count, INT32& low, INT32& high)
	{
		if (count < 5)
		{
			low = 1;
			high = 4;
		}
		else if (count < 10)
		{
			low = 5;
			high = 9;
		}
		else if (count < 20)
		{
			low = 10;
			high = 19;
		}
		else if (count < 50)
		{
			low = 20;
			high = 49;
		}
		else if (count < 100)
		{
			low = 50;
			high = 99;
		}
		else if (count < 250)
		{
			low = 100;
			high = 249;
		}
		else if (count < 500)
		{
			low = 250;
			high = 499;
		}
		else if (count < 1000)
		{
			low = 500;
			high = 999;
		}
		else
		{
			low = 1000;
			high = -1;
		}
	}

	LPCSTR GetThreatLabel(double ratio)
	{
		if (ratio < 0.1)
			return "Effortless";
		if (ratio < 0.25)
			return "Very Weak";
		if (ratio < 0.6)
			return "Weak";
		if (ratio < 0.9)
			return "A bit weaker";
		if (ratio < 1.1)
			return "Equal";
		if (ratio < 1.3)
			return "A bit stronger";
		if (ratio < 1.8)
			return "Strong";
		if (ratio < 2.5)
			return "Very Strong";
		if (ratio < 4.0)
			return "Challenging";
		if (ratio < 8.0)
			return "Overpowering";
		if (ratio < 20.0)
			return "Deadly";
		return "Impossible";
	}

	UINT64 GetArmyStrength(const H3Army& army)
	{
		UINT64 strength = 0;
		for (INT32 i = 0; i < 7; ++i)
		{
			if (IsCreatureId(army.type[i]) && army.count[i] > 0)
				strength += UINT64(army.count[i]) * UINT64(P_Creatures()[army.type[i]].aiValue);
		}
		return strength;
	}

	BOOL HasKnownCombatSpell(const H3Hero& hero)
	{
		for (INT32 i = NH3Spells::QUICK_SAND; i <= NH3Spells::AIR_ELEMENTAL; ++i)
		{
			if (hero.learnedSpells[i])
				return TRUE;
		}
		return FALSE;
	}

	double GetHeroStrength(const H3Hero& hero)
	{
		const double attack = hero.primarySkill[NH3Skills::ATTACK];
		const double defense = hero.primarySkill[NH3Skills::DEFENSE];
		const double spellPower = hero.primarySkill[NH3Skills::SPELL_POWER];
		const double knowledge = hero.primarySkill[NH3Skills::KNOWLEDGE];
		const double fightingStrength = std::sqrt((1.0 + 0.05 * attack) * (1.0 + 0.05 * defense));

		if (hero.spellPoints <= 0 || knowledge <= 0 || !HasKnownCombatSpell(hero))
			return fightingStrength;

		double manaRatio = static_cast<double>(hero.spellPoints) / (knowledge * 10.0);
		if (manaRatio < 0.0)
			manaRatio = 0.0;
		else if (manaRatio > 1.0)
			manaRatio = 1.0;

		const double magicStrength = std::sqrt((1.0 + 0.05 * knowledge * manaRatio) * (1.0 + 0.05 * spellPower * manaRatio));
		return fightingStrength * magicStrength;
	}

	double GetHeroTotalStrength(const H3Hero& hero)
	{
		return GetHeroStrength(hero) * static_cast<double>(GetArmyStrength(hero.army));
	}

	BOOL GetThreatText(double count, const H3CreatureInformation& creature, PCHAR buffer, UINT bufferSize)
	{
		H3Player* player = P_ActivePlayer();
		if (!player || player->currentHero < 0)
			return FALSE;

		H3Hero* hero = player->GetActiveHero();
		if (!hero)
			return FALSE;

		const double heroPower = GetHeroTotalStrength(*hero);
		if (heroPower <= 0.0 || count <= 0.0)
			return FALSE;

		const double monsterPower = count * double(creature.aiValue);
		const double ratio = monsterPower / heroPower;
		F_sprintfbuffer(buffer, "%s", GetThreatLabel(ratio));
		return TRUE;
	}

	constexpr DWORD HOTA_BAND_CALL = 0x40C2E1;
	const BYTE kStockBandCall[5] = { 0xE8, 0xCA, 0xE7, 0x03, 0x00 };

	const H3MapItem* g_hotaCountItem;
	INT32 g_hotaCount;

	BOOL ParseHotaBand(LPCSTR text, double& count)
	{
		for (LPCSTR p = strchr(text, '('); p; p = strchr(p + 1, '('))
		{
			INT32 low = 0;
			INT32 high = 0;
			if (sscanf_s(p, "(%d-%d)", &low, &high) == 2 && low > 0 && high >= low)
			{
				count = (low + high) / 2.0;
				return TRUE;
			}
			CHAR sign = 0;
			if (sscanf_s(p, "(%d%c", &low, &sign, 1) == 2 && sign == '+' && low > 0)
			{
				count = low;
				return TRUE;
			}
		}
		return FALSE;
	}

	void WriteHotaMonsterText(const H3MapItem& item, const H3CreatureInformation& creature)
	{
		const INT32 level = creature.level + 1;
		CHAR original[512];
		CHAR threat[64];
		double count = 0.0;
		lstrcpynA(original, h3_TextBuffer, sizeof(original));
		const BOOL known = &item == g_hotaCountItem && g_hotaCount > 0;
		if (known)
			count = g_hotaCount;
		if ((known || ParseHotaBand(original, count)) && GetThreatText(count, creature, threat, sizeof(threat)))
			F_sprintf("%s (lvl %d, %s)", original, level, threat);
		else
			F_sprintf("%s (lvl %d)", original, level);
	}

	void WriteMonsterText(const H3MapItem& item)
	{
		if (!IsMonsterObject(item.objectType) || !IsCreatureId(item.objectSubtype))
			return;

		if (IsHotA())
		{
			WriteHotaMonsterText(item, P_Creatures()[item.objectSubtype]);
			return;
		}

		const MapMonster& monster = *reinterpret_cast<const MapMonster*>(&item.setup);
		const H3CreatureInformation& creature = P_Creatures()[item.objectSubtype];
		const INT32 level = creature.level + 1;
		INT32 low = 0;
		INT32 high = 0;
		CHAR original[512];
		CHAR threat[64];
		const BOOL hasThreat = GetThreatText(double(monster.count), creature, threat, sizeof(threat));

		GetQuantityRange(monster.count, low, high);
		lstrcpynA(original, h3_TextBuffer, sizeof(original));
		if (high < 0)
		{
			if (hasThreat)
				F_sprintf("%s (%d+, lvl %d, %s)", original, low, level, threat);
			else
				F_sprintf("%s (%d+, lvl %d)", original, low, level);
		}
		else
		{
			if (hasThreat)
				F_sprintf("%s (%d-%d, lvl %d, %s)", original, low, high, level, threat);
			else
				F_sprintf("%s (%d-%d, lvl %d)", original, low, high, level);
		}
	}
}

int MonsterHoverInfoPlugin::ShowMonsterHoverInfo(HookContext& c)
{
	H3AdventureManager* adventure = P_AdventureMgr();
	H3MapItem* item = adventure->GetMapItem();

	if (item)
		WriteMonsterText(*item);

	return EXEC_DEFAULT;
}

int MonsterHoverInfoPlugin::CaptureHotaCount(HookContext& c)
{
	g_hotaCountItem = reinterpret_cast<const H3MapItem*>(c.ebx);
	g_hotaCount = INT32(c.ecx);
	return EXEC_DEFAULT;
}

_LHF_(CaptureHotaCount)
{
	return MonsterHoverInfo.CaptureHotaCount(*c);
}

_LHF_(ShowMonsterHoverInfo)
{
	return MonsterHoverInfo.ShowMonsterHoverInfo(*c);
}

void MonsterHoverInfoPlugin::Start()
{
	Hook(0x40D0DB, ::ShowMonsterHoverInfo);
	if (IsHotA() && memcmp(reinterpret_cast<const void*>(HOTA_BAND_CALL), kStockBandCall, sizeof(kStockBandCall)) == 0)
		Hook(HOTA_BAND_CALL, ::CaptureHotaCount);
}
