#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include "launcher_patch.h"

// RVAs in HD_LauncherNative.dll sha256 875f2ae7...ed64aa (HD update of 2026-10-25)
namespace
{
	constexpr DWORD RVA_LOAD_HOTA_EXIT_JMP  = 0x9826;
	constexpr DWORD RVA_LOAD_PACKS_FILL     = 0x98E9;
	constexpr DWORD RVA_SAVE_HOTA_JE        = 0xA120;
	constexpr DWORD RVA_SAVE_HOTA_EXIT_JMP  = 0xA147;
	constexpr DWORD RVA_SAVE_PACKS          = 0xA1E2;
	constexpr DWORD RVA_VECTOR_AT           = 0xE692;
	constexpr DWORD RVA_PROFILES_VECTOR     = 0x3C2E4;
	constexpr DWORD RVA_PROFILE_INDEX       = 0x378D0;
	constexpr DWORD RVA_MAIN_TAB_HWND       = 0x3C044;
	constexpr DWORD RVA_PACKS_KEY_STRING    = 0x30BE8;
	constexpr DWORD RVA_IAT_SHOWWINDOW      = 0x29334;

	DWORD g_profileIndex;
	DWORD g_profilesVector;
	DWORD g_vectorAt;
	DWORD g_loadPacksFill;
	DWORD g_savePacks;
	HWND* g_mainTab;

	decltype(&ShowWindow) g_realShowWindow;
	LogFn g_log;

	void Logf(const char* fmt, DWORD a = 0)
	{
		char buf[192];
		sprintf_s(buf, fmt, a);
		g_log(buf);
	}

	__declspec(naked) void LoadPacksStub()
	{
		__asm
		{
			mov  eax, g_profileIndex
			push dword ptr [eax]
			mov  ecx, g_profilesVector
			call g_vectorAt
			mov  edi, [eax]
			jmp  g_loadPacksFill
		}
	}

	__declspec(naked) void SavePacksStub()
	{
		__asm
		{
			xor edi, edi
			jmp g_savePacks
		}
	}

	struct DialogUnits
	{
		int x, y, w, h;
	};

	struct PackControl
	{
		int id;
		DialogUnits stock;
		DialogUnits hota;
	};

	const PackControl kPackControls[] = {
		{ 0x3FB, { 249, 27, 156, 129 }, { 249, 27, 156, 101 } },
		{ 0x40A, { 254, 39, 143,  78 }, { 254, 39, 143,  52 } },
		{ 0x3FC, { 254, 120, 93,  15 }, { 254, 94,  93,  15 } },
		{ 0x3FD, { 254, 135, 93,  15 }, { 254, 109, 93,  15 } },
		{ 0x3FE, { 352, 120, 47,  15 }, { 352, 94,  47,  15 } },
		{ 0x3FF, { 352, 135, 47,  15 }, { 352, 109, 47,  15 } },
	};

	const PackControl* FindPackControl(HWND hWnd)
	{
		if (!g_mainTab || !*g_mainTab || GetParent(hWnd) != *g_mainTab)
			return nullptr;
		const int id = GetDlgCtrlID(hWnd);
		for (const PackControl& c : kPackControls)
			if (c.id == id)
				return &c;
		return nullptr;
	}

	void PlaceControl(HWND hWnd, const DialogUnits& dlu)
	{
		RECT r = { dlu.x, dlu.y, dlu.x + dlu.w, dlu.y + dlu.h };
		MapDialogRect(GetParent(hWnd), &r);
		MoveWindow(hWnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
	}

	BOOL WINAPI HookShowWindow(HWND hWnd, int nCmdShow)
	{
		if (const PackControl* c = FindPackControl(hWnd))
		{
			if (nCmdShow == SW_HIDE)
			{
				PlaceControl(hWnd, c->hota);
				nCmdShow = SW_SHOW;
			}
			else
				PlaceControl(hWnd, c->stock);
		}
		return g_realShowWindow(hWnd, nCmdShow);
	}

	bool BytesMatch(BYTE* base, DWORD rva, const BYTE* expected, size_t size)
	{
		if (memcmp(base + rva, expected, size) == 0)
			return true;
		Logf("[HotaLoader] launcher patch: unexpected bytes at RVA %05X", rva);
		return false;
	}

	bool PushImmMatches(BYTE* base, DWORD rva, DWORD imm)
	{
		BYTE expected[5] = { 0x68 };
		memcpy(expected + 1, &imm, 4);
		return BytesMatch(base, rva, expected, sizeof(expected));
	}

	void WriteCode(BYTE* at, const void* bytes, size_t size)
	{
		DWORD old = 0;
		VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old);
		memcpy(at, bytes, size);
		VirtualProtect(at, size, old, &old);
		FlushInstructionCache(GetCurrentProcess(), at, size);
	}

	void WriteJump(BYTE* at, const BYTE* opcode, size_t opcodeSize, const void* target)
	{
		BYTE code[6] = {};
		memcpy(code, opcode, opcodeSize);
		const INT32 rel = INT32(reinterpret_cast<const BYTE*>(target) - (at + opcodeSize + 4));
		memcpy(code + opcodeSize, &rel, 4);
		WriteCode(at, code, opcodeSize + 4);
	}
}

bool PatchHdLauncher(HMODULE native, LogFn log)
{
	g_log = log;
	BYTE* base = reinterpret_cast<BYTE*>(native);

	static const BYTE loadExitJmp[] = { 0xE9, 0x93, 0x01, 0x00, 0x00 };
	static const BYTE saveHotaJe[]  = { 0x0F, 0x84, 0x71, 0x01, 0x00, 0x00 };
	static const BYTE saveExitJmp[] = { 0xE9, 0x4B, 0x01, 0x00, 0x00 };
	static const BYTE vectorAt[]    = { 0x8B, 0x09, 0x8B, 0x44, 0x24, 0x04, 0x8D, 0x04, 0x81, 0xC2, 0x04, 0x00 };

	const DWORD packsKey = DWORD(base + RVA_PACKS_KEY_STRING);
	auto realShowWindow = reinterpret_cast<decltype(&ShowWindow)>(GetProcAddress(GetModuleHandleA("user32.dll"), "ShowWindow"));
	auto iatShowWindow = reinterpret_cast<decltype(&ShowWindow)*>(base + RVA_IAT_SHOWWINDOW);

	if (!BytesMatch(base, RVA_LOAD_HOTA_EXIT_JMP, loadExitJmp, sizeof(loadExitJmp))
		|| !BytesMatch(base, RVA_SAVE_HOTA_JE, saveHotaJe, sizeof(saveHotaJe))
		|| !BytesMatch(base, RVA_SAVE_HOTA_EXIT_JMP, saveExitJmp, sizeof(saveExitJmp))
		|| !BytesMatch(base, RVA_VECTOR_AT, vectorAt, sizeof(vectorAt))
		|| !PushImmMatches(base, RVA_LOAD_PACKS_FILL, packsKey)
		|| !PushImmMatches(base, RVA_SAVE_PACKS, packsKey))
		return false;
	if (!realShowWindow || *iatShowWindow != realShowWindow)
	{
		Logf("[HotaLoader] launcher patch: ShowWindow import slot holds %08X", DWORD(*iatShowWindow));
		return false;
	}

	g_profileIndex    = DWORD(base + RVA_PROFILE_INDEX);
	g_profilesVector  = DWORD(base + RVA_PROFILES_VECTOR);
	g_vectorAt        = DWORD(base + RVA_VECTOR_AT);
	g_loadPacksFill   = DWORD(base + RVA_LOAD_PACKS_FILL);
	g_savePacks       = DWORD(base + RVA_SAVE_PACKS);
	g_mainTab         = reinterpret_cast<HWND*>(base + RVA_MAIN_TAB_HWND);
	g_realShowWindow  = realShowWindow;

	static const BYTE jmp[] = { 0xE9 };
	static const BYTE je[]  = { 0x0F, 0x84 };
	WriteJump(base + RVA_LOAD_HOTA_EXIT_JMP, jmp, sizeof(jmp), &LoadPacksStub);
	WriteJump(base + RVA_SAVE_HOTA_JE, je, sizeof(je), &SavePacksStub);
	WriteJump(base + RVA_SAVE_HOTA_EXIT_JMP, jmp, sizeof(jmp), &SavePacksStub);

	auto hook = &HookShowWindow;
	WriteCode(reinterpret_cast<BYTE*>(iatShowWindow), &hook, sizeof(hook));
	return true;
}
