#include "KeyRemap.hpp"
#include <stdio.h>

using namespace h3;

KeyRemapPlugin KeyRemap;

KeyRemapPlugin& KeyRemapPlugin::GetPlugin()
{
	return KeyRemap;
}

namespace
{
	constexpr DWORD WASD_SCROLL_INTERVAL_MS = 60;

	bool g_wasd_down[4] = {};
	DWORD g_last_wasd_scroll_tick = 0;
	HiHook* g_get_async_key_state_hook = nullptr;

	// HOTA removed SoD's F9/Shift+F9 quicksave-quickload key handler (confirmed: F9 is a
	// no-op in HOTA), so the SoD-style remap that forges F9 messages has nothing to land
	// on. Under HOTA we instead call SaveGame directly with a fixed name that always
	// overwrites (single quicksave slot, no multiple files).
	bool IsHota()
	{
		static const bool is_hota = ::GetModuleHandleA("HD_HOTA.dll") != nullptr;
		return is_hota;
	}

	// HOTA keeps the original in-exe H3Main save/load routines at 0x4BEB60/0x4BEFF0.
	// The earlier H3API "HOTA relocated SaveGame" guess at 0x8BEB60 is dead here.
	// 0x602D60 is only the modal dialog/event-loop helper used by the menu; calling it
	// with a copied dialog-manager snapshot crashed because list pointers past the
	// copied bytes were no longer live. Save can call the real H3Main routine directly.
	// Load must not call raw 0x4BEFF0 or the scenario-list routine 0x4F0720: the first
	// reads with errors from a fresh/new-game state, and the second crashes unless the
	// surrounding menu context has initialized globals such as 0x699568.
	const char HOTA_QUICKSAVE_NAME[] = "quicksave";

	bool HotaQuickSave()
	{
		H3Main* main = P_Main();
		if (!main)
			return false;

		return THISCALL_6(BOOL, 0x4BEB60, main, HOTA_QUICKSAVE_NAME, 1, 1, 1, 0) != FALSE;
	}

	// HOTA quickload = SoD_SP's longjmp trick, verified against the live game with
	// cdb. The outer session function (same addresses in SoD and HOTA) does:
	//     0x4F0506  mov  ecx, 0x69D680
	//     ...
	//     0x4F0510  mov  ecx, [0x699550]        ; executive manager
	//     0x4F0516  call 0x4B0BA0               ; runs the whole game session
	//     0x4F051B  ...                         ; post-session teardown (removes
	//                                           ; advManager etc.), then the outer
	//                                           ; menu loop reads [0x697728]
	// A cdb stack dump of the running game (HD-mod included) showed the adventure
	// map executing under exactly this frame: return address 0x4F051B. SoD_SP's
	// Shift+F9 captures esp/ebp/edi with a LoHook at 0x4F0506; quickload resets a
	// little transient state, sets [0x697728] = 0x66 ("next: load game"), restores
	// those registers and jumps to 0x4F051B - pretending the session function just
	// returned. Teardown and the menu loop then bring up the load flow legitimately.
	// Replicated 1:1 below (SoD_SP's own quickload proc sits at its 0x10008670).
	constexpr DWORD HOTA_SESSION_CAPTURE  = 0x4F0506;
	constexpr DWORD HOTA_SESSION_TEARDOWN = 0x4F051B;

	DWORD hota_session_esp = 0;
	DWORD hota_session_ebp = 0;
	DWORD hota_session_edi = 0;
	// F9 only sets this; the longjmp itself runs from the FilterRawInput hook at the
	// top of the executive loop's tick. Jumping straight out of the key handler
	// crashed (HD crash log, 0x4631BB): the teardown after 0x4F051B pumps input
	// again, and doing that with the original input dispatch abandoned mid-flight
	// re-entered the adventure manager's reset at the wrong time.
	bool hota_quickload_requested = false;

	int __stdcall CaptureHotaSessionFrame(LoHook* h, HookContext* c)
	{
		hota_session_esp = static_cast<DWORD>(c->esp);
		hota_session_ebp = static_cast<DWORD>(c->ebp);
		hota_session_edi = static_cast<DWORD>(c->edi);
		return EXEC_DEFAULT;
	}

	__declspec(naked) void JumpToHotaSessionTeardown()
	{
		__asm
		{
			mov ebp, hota_session_ebp
			mov esp, hota_session_esp
			mov edi, hota_session_edi
			xor esi, esi
			mov eax, HOTA_SESSION_TEARDOWN
			jmp eax
		}
	}

	bool HotaQuickLoad()
	{
		H3Main* main = P_Main();
		if (!main || !hota_session_esp || !hota_session_ebp)
			return false;

		// State resets copied from SoD_SP's quickload proc; same statics in HOTA.
		// 1. Clear the static vector at 0x696A08 (free its block via the game's
		//    free at 0x60B0F0, zero begin/end/capacity).
		DWORD* vec = reinterpret_cast<DWORD*>(0x696A08);
		if (vec[1])
			CDECL_1(VOID, 0x60B0F0, vec[1]);
		vec[1] = vec[2] = vec[3] = 0;
		// 2. Detach the combat manager so a battle quickload leaves nothing behind.
		THISCALL_2(VOID, 0x4B0950,
			*reinterpret_cast<DWORD*>(0x699550), *reinterpret_cast<DWORD*>(0x699420));
		// 3. No pending adventure-map battle.
		if (P_AdventureMgr())
		{
			P_AdventureMgr()->monsterTypeBattle  = -1;
			P_AdventureMgr()->monsterCountBattle = -1;
		}
		// 4. Reset the +0x10 field of the 0x69D680 object (the one 0x4F0506 touches).
		*reinterpret_cast<DWORD*>(0x69D680 + 0x10) = 0;
		// 5. Tell the outer menu loop what to do after teardown: load game.
		*reinterpret_cast<INT32*>(0x697728) = 0x66;

		JumpToHotaSessionTeardown(); // does not return
		return true;
	}

	bool IsPhysicalWasd(int virtual_key)
	{
		switch (virtual_key)
		{
		case 'W':
		case 'A':
		case 'S':
		case 'D':
			return true;
		default:
			return false;
		}
	}

	// Screen detection rests on two authoritative signals, no guesswork:
	//
	// 1. The current top manager's NAME string (H3Manager::name at +0x14). H3
	//    keeps this accurate. A manager whose name is its own screen
	//    (townManager / combatManager / advManager) owns the screen outright,
	//    even when its base dialog is momentarily not the top of the dialog
	//    chain (e.g. townManager reports its dlg non-top right after a hero
	//    transfer screen closes).
	//
	// 2. Whether that screen's BASE DIALOG is the top of the global dialog
	//    linked list - H3BasicDlg::IsTopDialog() == (nextDialog == nullptr).
	//    This is the real "is this screen interactive / nothing stacked above"
	//    test, and is needed to disambiguate the SHARED heroWindowManager, which
	//    hosts adventure, town and combat sub-views at different times.
	//
	// The previous code used a "WindowMgrHasNoModal" check comparing the window
	// manager's firstDlg/lastDlg (+0x50/+0x54). Those track the window manager's
	// own dialog span, NOT the global next-chain, so the comparison did not mean
	// "no modal" - it wrongly reported a stacked dialog for an interactive town
	// after a transfer and dropped every key. IsTopDialog is the correct primitive.

	// The current top manager's name string (H3Manager::name at +0x14).
	// Returns "" when unavailable. Verified live names: advManager, townManager,
	// combatManager, heroWindowManager, swapManager.
	const char* CurrentMgrName()
	{
		if (!P_ExecutiveMgr() || !P_ExecutiveMgr()->last_mgr)
			return "";
		return reinterpret_cast<const char*>(reinterpret_cast<UINT8*>(P_ExecutiveMgr()->last_mgr) + 0x14);
	}

	bool MgrNameIs(const char* name)
	{
		const char* cur = CurrentMgrName();
		for (int i = 0; i < 28; ++i)
		{
			if (cur[i] != name[i])
				return false;
			if (name[i] == 0)
				return true;
		}
		return true;
	}

	bool IsAdventureMapActive()
	{
		if (!P_ExecutiveMgr() || !P_AdventureMgr())
			return false;

		// advManager current => map active, UNLESS a menu (Save/Load/System) is
		// stacked on top - that dialog stays under advManager (no manager switch),
		// so the base dialog's IsTopDialog() is the only signal it's covered. Bug:
		// this branch used to return true outright, so WASD (and the save-name
		// text box) both grabbed 'S'/'D' while the Save Game dialog was open.
		if (MgrNameIs("advManager"))
			return P_AdventureMgr()->dlg && P_AdventureMgr()->dlg->IsTopDialog();
		// Map reached back via heroWindowManager once its hero window / exchange
		// closes: the adventure base dialog being top of the chain is the signal.
		if (MgrNameIs("heroWindowManager"))
			return P_AdventureMgr()->dlg && P_AdventureMgr()->dlg->IsTopDialog();
		return false;
	}

	bool IsTownScreenActive()
	{
		if (!P_ExecutiveMgr() || !P_TownMgr())
			return false;

		// townManager current => town active, even when its base dialog reports
		// townTop=0 (the state left right after a hero transfer screen closes).
		if (MgrNameIs("townManager"))
			return true;
		// Town reached back via heroWindowManager (e.g. right after a hero
		// transfer/swap screen closes). The town dialog being top is the reliable
		// signal here; WindowMgrHasNoModal is NOT (it reports a stacked dialog in
		// this state even though the town is interactive, which used to drop all
		// keys - confirmed in logs: noModal=0 townTop=1 branch=PASS).
		if (MgrNameIs("heroWindowManager"))
			return P_TownMgr()->dlg && P_TownMgr()->dlg->IsTopDialog();
		return false;
	}

	bool IsCombatScreenActive()
	{
		if (!P_ExecutiveMgr() || !P_CombatMgr())
			return false;

		if (MgrNameIs("combatManager"))
			return true;
		if (MgrNameIs("heroWindowManager"))
			return P_CombatMgr()->dlg && P_CombatMgr()->dlg->IsTopDialog();
		return false;
	}

	bool IsRawKeyMessage(const H3InputManager::InputMessages& msg)
	{
		return msg.message == NH3VKey::MT_KEY_DOWN || msg.message == NH3VKey::MT_KEY_UP;
	}

	bool IsRawKeyDown(const H3InputManager::InputMessages& msg)
	{
		return msg.message == NH3VKey::MT_KEY_DOWN;
	}

	bool IsRawKeyUp(const H3InputManager::InputMessages& msg)
	{
		return msg.message == NH3VKey::MT_KEY_UP;
	}

	int WasdIndex(int key)
	{
		switch (key)
		{
		case 'W':
			return 0;
		case 'A':
			return 1;
		case 'S':
			return 2;
		case 'D':
			return 3;
		default:
			return -1;
		}
	}

	void SetWasdKeyDown(int key, bool down)
	{
		const int index = WasdIndex(key);
		if (index >= 0)
			g_wasd_down[index] = down;
	}

	void ClearWasdKeys()
	{
		memset(g_wasd_down, 0, sizeof(g_wasd_down));
	}

	bool GetHeldWasdDirection(int& dx, int& dy)
	{
		dx = (g_wasd_down[3] ? 1 : 0) - (g_wasd_down[1] ? 1 : 0);
		dy = (g_wasd_down[2] ? 1 : 0) - (g_wasd_down[0] ? 1 : 0);
		return dx || dy;
	}

	void MarkWasdScrollTick(DWORD tick)
	{
		g_last_wasd_scroll_tick = tick;
	}

	bool IsPhysicalKeyDown(int key)
	{
		if (g_get_async_key_state_hook)
			return (STDCALL_1(SHORT, g_get_async_key_state_hook->GetDefaultFunc(), key) & 0x8000) != 0;

		return (::GetAsyncKeyState(key) & 0x8000) != 0;
	}

	void RefreshWasdKeys()
	{
		g_wasd_down[0] = IsPhysicalKeyDown('W');
		g_wasd_down[1] = IsPhysicalKeyDown('A');
		g_wasd_down[2] = IsPhysicalKeyDown('S');
		g_wasd_down[3] = IsPhysicalKeyDown('D');
	}

	int H3KeyToWindowsKey(int h3_key)
	{
		switch (h3_key)
		{
		case NH3VKey::H3VK_W:
			return 'W';
		case NH3VKey::H3VK_A:
			return 'A';
		case NH3VKey::H3VK_S:
			return 'S';
		case NH3VKey::H3VK_D:
			return 'D';
		default:
			return 0;
		}
	}

	// Raw key messages carry the actual Windows virtual-key in dwords[3];
	// dwords[0] / byte0 hold the scancode that the screens act on. Always match
	// on the VK from here, then write the replacement with ReplaceRawKey.
	int RawVirtualKey(const H3InputManager::InputMessages& msg)
	{
		return reinterpret_cast<const INT32*>(msg.f_4)[3];
	}

	int RawRemapKey(const H3InputManager::InputMessages& msg)
	{
		const int virtual_key = RawVirtualKey(msg);

		if (virtual_key == VK_TAB)
			return NH3VKey::H3VK_H;
		if (virtual_key == 'F')
			return NH3VKey::H3VK_T;

		return 0;
	}

	void ReplaceRawKey(H3InputManager::InputMessages& msg, int replacement_key)
	{
		UINT8* bytes = reinterpret_cast<UINT8*>(msg.f_4);
		INT32* dwords = reinterpret_cast<INT32*>(msg.f_4);

		dwords[0] = replacement_key;
		bytes[0] = UINT8(replacement_key);
	}

	int RawWasdKey(const H3InputManager::InputMessages& msg)
	{
		const UINT8* bytes = reinterpret_cast<const UINT8*>(msg.f_4);
		const INT32* dwords = reinterpret_cast<const INT32*>(msg.f_4);
		const int dword_key = H3KeyToWindowsKey(dwords[0]);
		if (dword_key || IsPhysicalWasd(dwords[0]))
			return dword_key ? dword_key : dwords[0];

		const int byte_key = H3KeyToWindowsKey(bytes[0]);
		if (byte_key || IsPhysicalWasd(bytes[0]))
			return byte_key ? byte_key : bytes[0];

		return 0;
	}

	bool IsRawFKey(const H3InputManager::InputMessages& msg)
	{
		const UINT8* bytes = reinterpret_cast<const UINT8*>(msg.f_4);
		const INT32* dwords = reinterpret_cast<const INT32*>(msg.f_4);

		return dwords[0] == 'F' || dwords[0] == NH3VKey::H3VK_F || bytes[0] == 'F' || bytes[0] == NH3VKey::H3VK_F;
	}

	volatile LONG g_enter_scheduled = 0;

	void SendEnterKey()
	{
		INPUT input[2] = {};
		input[0].type = INPUT_KEYBOARD;
		input[0].ki.wScan = 0x1C;
		input[0].ki.dwFlags = KEYEVENTF_SCANCODE;
		input[1].type = INPUT_KEYBOARD;
		input[1].ki.wScan = 0x1C;
		input[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
		SendInput(2, input, sizeof(INPUT));
	}

	DWORD WINAPI DelayedEnterThread(LPVOID)
	{
		SendEnterKey();
		InterlockedExchange(&g_enter_scheduled, 0);
		return 0;
	}

	void SendEnterKeyDelayedOnce()
	{
		if (InterlockedCompareExchange(&g_enter_scheduled, 1, 0) != 0)
			return;

		HANDLE thread = CreateThread(nullptr, 0, DelayedEnterThread, nullptr, 0, nullptr);
		if (thread)
			CloseHandle(thread);
		else
			InterlockedExchange(&g_enter_scheduled, 0);
	}

}

//===================================================================================
//
//                             HOOKS
//
//===================================================================================

bool KeyRemapPlugin::HandleKey(H3AdventureManager& adventure, H3Msg& msg) const
{
	using namespace NH3VKey;
	(void)adventure;

	switch (msg.KeyPressed())
	{
	case H3VK_TAB:
		msg.subtype = H3VK_H;
		return false;
	case H3VK_F:
		msg.subtype = H3VK_T;
		return false;
	default:
		return false;
	}
}

void KeyRemapPlugin::ProcessHeldWasd(H3AdventureManager& adventure) const
{
	RefreshWasdKeys();

	if (!IsAdventureMapActive())
	{
		ClearWasdKeys();
		return;
	}

	int dx = 0;
	int dy = 0;
	if (!GetHeldWasdDirection(dx, dy))
		return;

	const DWORD now = GetTickCount();
	if (now - g_last_wasd_scroll_tick < WASD_SCROLL_INTERVAL_MS)
		return;
	MarkWasdScrollTick(now);

	ScrollMap(adventure, dx, dy);
}

void KeyRemapPlugin::ScrollMap(H3AdventureManager& adventure, int dx, int dy) const
{
	static const INT32 directions[3][3] = {
		{ 7, 0, 1 },
		{ 6, -1, 2 },
		{ 5, 4, 3 },
	};
	const INT32 direction = directions[dy + 1][dx + 1];
	if (direction < 0)
		return;

	// Stock edge scroll; _HD3_.dll 60fps hooks at 0x419657/0x4196F3 animate it (dir 0..7 = N..NW).
	THISCALL_3(VOID, 0x419520, &adventure, direction, 0);
}

bool KeyRemapPlugin::HandleWasdRawInput(H3AdventureManager& adventure, H3InputManager::InputMessages& msg) const
{
	const int key = RawWasdKey(msg);
	if (!key)
		return false;

	(void)adventure;

	if (IsRawKeyDown(msg))
		SetWasdKeyDown(key, true);
	else if (IsRawKeyUp(msg))
	{
		SetWasdKeyDown(key, false);
	}

	msg.message = 0;
	memset(msg.f_4, 0, sizeof(msg.f_4));
	return true;
}

bool IsWasdKey(H3Msg& msg)
{
	using namespace NH3VKey;

	switch (msg.KeyPressed())
	{
	case H3VK_W:
	case H3VK_A:
	case H3VK_S:
	case H3VK_D:
		return true;
	default:
		return false;
	}
}

int __stdcall KeyRemapPlugin::RemapAdventureKeys(HiHook* h, H3AdventureManager* This, H3Msg* msg, int a3, int a4, int a5)
{
	if (This && msg && (msg->IsKeyDown() || msg->IsKeyHeld()) && !IsWasdKey(*msg))
	{
		if (HandleKey(*This, *msg))
		{
			return msg->StopProcessing();
		}
	}
	else if (msg && msg->IsKeyPress() && IsWasdKey(*msg))
	{
		return msg->StopProcessing();
	}

	return THISCALL_5(int, h->GetDefaultFunc(), This, msg, a3, a4, a5);
}

int __stdcall _HH_RemapAdventureKeys(HiHook* h, H3AdventureManager* This, H3Msg* msg, int a3, int a4, int a5)
{
	return KeyRemap.RemapAdventureKeys(h, This, msg, a3, a4, a5);
}

// TEMP CAPTURE HOOK - logs SaveGame(0x8beb60) args to disk, then calls through unchanged.
// Remove once the real args are captured for the HOTA quicksave rewrite.
int __stdcall _HH_CaptureSaveGame(HiHook* h, void* This, int a1, int a2, int a3, int a4, int a5)
{
	FILE* f = nullptr;
	fopen_s(&f, "SaveGameCapture.log", "a");
	if (f)
	{
		fprintf(f, "SaveGame this=%p a1=%p(\"%s\") a2=%d a3=%d a4=%d a5=%d\n",
			This, reinterpret_cast<void*>(a1),
			a1 ? reinterpret_cast<const char*>(a1) : "",
			a2, a3, a4, a5);
		fclose(f);
	}
	return THISCALL_6(int, h->GetDefaultFunc(), This, a1, a2, a3, a4, a5);
}

int __stdcall KeyRemapPlugin::FilterRawInput(HiHook* h, H3InputManager* This, int* a2)
{
	// Deferred quickload: 0x4EC660 is called at the top of every executive-loop
	// tick, so the stack here is just [exec loop] -> [bridge] -> [this hook], with
	// no input dispatch in flight. Safe place to abandon the session. The flag is
	// cleared inside HotaQuickLoad's caller path via the reset below BEFORE the
	// longjmp, so the teardown's own input pumping cannot re-trigger it.
	if (hota_quickload_requested)
	{
		hota_quickload_requested = false;
		HotaQuickLoad(); // does not return when it succeeds
	}

	bool send_enter_after_default = false;
	H3AdventureManager* adventure = nullptr;
	if (IsAdventureMapActive())
		adventure = P_AdventureMgr();

	if (This)
	{
		auto& raw_msg = This->GetCurrentMessage();

		if (adventure && IsRawKeyMessage(raw_msg))
		{
			// Adventure quick save/load (SOD: F9 = quick save, Shift+F9 = quick load):
			//   F5 -> F9       (quick save)  - rewrite the message in place.
			//   F9 -> Shift+F9 (quick load)  - keep F9 but set the message's shift
			//                                  modifier so the handler reads Shift+F9.
			// Keys carry H3VK in dwords[0]/byte0 and Windows VK in dwords[3]; the
			// shift modifier the load handler reads lives in dwords[2] (1 = held),
			// NOT in GetKeyState. Confirmed by logging the raw message.
			if (IsHota())
			{
				INT32* dwords = reinterpret_cast<INT32*>(raw_msg.f_4);
				if (IsRawKeyDown(raw_msg) && dwords[3] == VK_F5)
				{
					HotaQuickSave();
					raw_msg.message = 0;
					memset(raw_msg.f_4, 0, sizeof(raw_msg.f_4));
				}
				else if (IsRawKeyDown(raw_msg) && dwords[3] == VK_F9)
				{
					raw_msg.message = 0;
					memset(raw_msg.f_4, 0, sizeof(raw_msg.f_4));
					hota_quickload_requested = true;
				}
			}
			else
			{
				INT32* dwords = reinterpret_cast<INT32*>(raw_msg.f_4);
				if (dwords[3] == VK_F5)
				{
					dwords[0] = NH3VKey::H3VK_F9;
					reinterpret_cast<UINT8*>(raw_msg.f_4)[0] = UINT8(NH3VKey::H3VK_F9);
					dwords[3] = VK_F9;
				}
				else if (dwords[3] == VK_F9)
				{
					// The quick-load handler reads the shift modifier from the
					// message's dwords[2] (1 = shift held), NOT from GetKeyState.
					// Set it so plain F9 is seen as Shift+F9 (quick load).
					dwords[2] = 1;
				}
			}

			send_enter_after_default = IsRawKeyUp(raw_msg) && IsRawFKey(raw_msg);
			const int remap_key = RawRemapKey(raw_msg);
			if (remap_key)
				ReplaceRawKey(raw_msg, remap_key);

			HandleWasdRawInput(*adventure, raw_msg);
		}
		else if (IsTownScreenActive() && IsRawKeyMessage(raw_msg))
		{
			ClearWasdKeys();

			// Match on the VK (see RawVirtualKey), apply via ReplaceRawKey.
			const int vk = RawVirtualKey(raw_msg);

			if (vk == 'F')
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_H); // F -> town hall (H)
			}
			else if (vk == VK_SPACE)
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_ENTER); // SPACE -> Enter
			}
			else if (vk == 'E')
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_S); // E -> S (swap so left hand stays on WASD)
			}
			else if (vk == 'S' || vk == 'D')
			{
				// Swallow physical S / D so they do nothing on the town screen.
				raw_msg.message = 0;
				memset(raw_msg.f_4, 0, sizeof(raw_msg.f_4));
			}
		}
		else if (IsCombatScreenActive() && IsRawKeyMessage(raw_msg))
		{
			ClearWasdKeys();

			// The battle-result screen (victory / defeat) is a separate dialog
			// stacked on top of the combat manager's own base dialog. During an
			// active battle the combat dialog is the top of the chain; on the
			// result screen it is not. Switch remaps on that:
			//   active battle : SPACE -> W (wait), Q -> D (defend)
			//   result screen : SPACE -> ENTER, F -> ESC
			const bool battle_active = P_CombatMgr()->dlg && P_CombatMgr()->dlg->IsTopDialog();

			// Match on the VK (see RawVirtualKey), apply via ReplaceRawKey.
			const int vk = RawVirtualKey(raw_msg);

			if (!battle_active)
			{
				if (vk == VK_SPACE)
					ReplaceRawKey(raw_msg, NH3VKey::H3VK_ENTER);
				else if (vk == 'F')
					ReplaceRawKey(raw_msg, NH3VKey::H3VK_ESCAPE);
			}
			else if (vk == VK_SPACE)
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_W);
			}
			else if (vk == 'Q')
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_D);
			}
			else if (vk == 'G')
			{
				ReplaceRawKey(raw_msg, NH3VKey::H3VK_A); // G -> auto-combat
			}
		}
		else if (!IsAdventureMapActive())
		{
			ClearWasdKeys();
		}
	}

	const int result = THISCALL_2(int, h->GetDefaultFunc(), This, a2);
	if (adventure)
		ProcessHeldWasd(*adventure);
	if (send_enter_after_default)
		SendEnterKeyDelayedOnce();

	return result;
}

int __stdcall _HH_FilterRawInput(HiHook* h, H3InputManager* This, int* a2)
{
	return KeyRemap.FilterRawInput(h, This, a2);
}

SHORT __stdcall KeyRemapPlugin::FilterGetKeyState(HiHook* h, int virtual_key)
{
	if (IsAdventureMapActive() && IsPhysicalWasd(virtual_key))
		return 0;
	if (IsTownScreenActive() && (virtual_key == VK_SPACE || virtual_key == 'S' || virtual_key == 'D'))
		return 0;

	return STDCALL_1(SHORT, h->GetDefaultFunc(), virtual_key);
}

SHORT __stdcall KeyRemapPlugin::FilterGetAsyncKeyState(HiHook* h, int virtual_key)
{
	g_get_async_key_state_hook = h;

	if (IsAdventureMapActive() && IsPhysicalWasd(virtual_key))
		return 0;
	if (IsTownScreenActive() && (virtual_key == VK_SPACE || virtual_key == 'S' || virtual_key == 'D'))
		return 0;

	return STDCALL_1(SHORT, h->GetDefaultFunc(), virtual_key);
}

BOOL __stdcall KeyRemapPlugin::FilterGetKeyboardState(HiHook* h, PBYTE key_state)
{
	const BOOL result = STDCALL_1(BOOL, h->GetDefaultFunc(), key_state);

	if (result && key_state && IsAdventureMapActive())
	{
		key_state['W'] = 0;
		key_state['A'] = 0;
		key_state['S'] = 0;
		key_state['D'] = 0;
	}
	if (result && key_state && IsTownScreenActive())
	{
		key_state[VK_SPACE] = 0;
		key_state['S'] = 0;
		key_state['D'] = 0;
	}

	return result;
}

SHORT __stdcall _HH_FilterGetKeyState(HiHook* h, int virtual_key)
{
	return KeyRemap.FilterGetKeyState(h, virtual_key);
}

SHORT __stdcall _HH_FilterGetAsyncKeyState(HiHook* h, int virtual_key)
{
	return KeyRemap.FilterGetAsyncKeyState(h, virtual_key);
}

BOOL __stdcall _HH_FilterGetKeyboardState(HiHook* h, PBYTE key_state)
{
	return KeyRemap.FilterGetKeyboardState(h, key_state);
}

/**
 * @brief This member function is used to install all your hooks.
 */
void KeyRemapPlugin::Start()
{
	Hook(0x4EC660, Splice, Thiscall, ::_HH_FilterRawInput);
	Hook(0x408BA0, Splice, Thiscall, ::_HH_RemapAdventureKeys);
	// HOTA only: capture the session frame for F9 quickload's longjmp. Same spot
	// SoD_SP hooks; under SoD, SoD_SP itself provides quickload so stay out of it.
	if (IsHota())
		Hook(HOTA_SESSION_CAPTURE, CaptureHotaSessionFrame);
	Hook(reinterpret_cast<DWORD>(::GetKeyState), Splice, Stdcall, ::_HH_FilterGetKeyState);
	Hook(reinterpret_cast<DWORD>(::GetAsyncKeyState), Splice, Stdcall, ::_HH_FilterGetAsyncKeyState);
	Hook(reinterpret_cast<DWORD>(::GetKeyboardState), Splice, Stdcall, ::_HH_FilterGetKeyboardState);
}
