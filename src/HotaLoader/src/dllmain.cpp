//====================================================================================
//
// HotaLoader - version.dll proxy that force-loads H3.KeyRemap.dll under HOTA.
//
// HOTA (h3hota.exe / h3hota HD.exe) runs the HD-Mod loader but does NOT enable the
// plugin-pack DLL loader for the HOTA profile, so packs listed in hota.ini never
// load. This proxy provides an engine-agnostic "way in":
//
//   - It is named version.dll and placed in the game folder. HOTA loads version.dll
//     early by name (not a Windows KnownDLL, so the game-folder copy wins the search
//     order). All 17 exports are forwarded to verreal.dll (a copy of the real
//     system version.dll placed alongside), so version APIs keep working.
//   - On attach it spawns a worker thread that waits for the game's main window and
//     a short settle, then LoadLibrary's H3.KeyRemap.dll. KeyRemap's hooks then
//     splice at the engine addresses (verified identical between SoD and HOTA).
//
// Timing matters: LoadLibrary must NOT happen in DllMain (loader-lock deadlock), and
// must NOT happen while HD_HOTA is still initialising or while the main thread is
// mid-execution (patcher_x86 hook installation races and corrupts). Waiting for the
// window + settle lands the load in the idle menu state.
//
// Install:   copy real system version.dll -> <game>\verreal.dll
//            copy this build               -> <game>\version.dll
// Uninstall: delete <game>\version.dll and <game>\verreal.dll
//
//====================================================================================

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include "forwarders.h"
#include "launcher_patch.h"
#include "ext_lod.h"

namespace
{
	// Build a path next to the running game exe.
	void GameRelative(char* out, size_t out_size, const char* leaf)
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		char* slash = strrchr(exe, '\\');
		if (slash)
			*(slash + 1) = 0;
		else
			exe[0] = 0;
		_snprintf_s(out, out_size, _TRUNCATE, "%s%s", exe, leaf);
	}

	void Log(const char* msg)
	{
		char path[MAX_PATH] = {};
		GameRelative(path, sizeof(path), "HotaLoader.log");
		FILE* f = nullptr;
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		const char* exe_name = strrchr(exe, '\\');
		exe_name = exe_name ? exe_name + 1 : exe;
		if (fopen_s(&f, path, "a") == 0 && f)
		{
			fprintf(f, "[%s pid=%lu] %s\n", exe_name, GetCurrentProcessId(), msg);
			fclose(f);
		}
	}

	bool WaitForGameWindow()
	{
		for (int i = 0; i < 600; ++i) // up to ~60s
		{
			HWND hwnd = FindWindowA("Heroes III", nullptr);
			if (hwnd && IsWindowVisible(hwnd))
			{
				DWORD pid = 0;
				GetWindowThreadProcessId(hwnd, &pid);
				if (pid == GetCurrentProcessId())
				{
					// Settle until the main thread is idle at the menu before hooking.
					Sleep(4000);
					return true;
				}
			}
			Sleep(100);
		}
		return false;
	}

	struct HookGuard
	{
		const char* dll;
		DWORD       hookAddress;
		BYTE        stockBytes[5];
	};

	const HookGuard kGuards[] = {
		{ "H3.MonsterHoverInfo.dll", 0x40D0DB, { 0x68, 0x28, 0x74, 0x69, 0x00 } },
	};

	bool HookSiteIsStock(const char* dll)
	{
		for (const HookGuard& guard : kGuards)
		{
			if (_stricmp(guard.dll, dll) != 0)
				continue;

			BYTE live[sizeof(guard.stockBytes)] = {};
			SIZE_T read = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(guard.hookAddress), live, sizeof(live), &read)
				|| read != sizeof(live))
			{
				char buf[128];
				sprintf_s(buf, "[HotaLoader] SKIPPED %s: %08X is not readable", dll, guard.hookAddress);
				Log(buf);
				return false;
			}
			if (memcmp(live, guard.stockBytes, sizeof(guard.stockBytes)) != 0)
			{
				char buf[160];
				sprintf_s(buf, "[HotaLoader] SKIPPED %s: %08X holds %02X %02X %02X %02X %02X, expected stock bytes",
					dll, guard.hookAddress, live[0], live[1], live[2], live[3], live[4]);
				Log(buf);
				return false;
			}
		}
		return true;
	}

	int ReadPackList(char names[][MAX_PATH], int maxNames)
	{
		char ini[MAX_PATH] = {};
		GameRelative(ini, sizeof(ini), "_HD3_Data\\Settings\\hota.ini");
		FILE* f = nullptr;
		if (fopen_s(&f, ini, "r") != 0 || !f)
		{
			Log("[HotaLoader] cannot open _HD3_Data\\Settings\\hota.ini");
			return 0;
		}

		int count = 0;
		char line[2048];
		while (fgets(line, sizeof(line), f))
		{
			if (strncmp(line, "<Packs>", 7) != 0)
				continue;
			for (char* p = strchr(line, '"'); p && count < maxNames; )
			{
				char* close = strchr(p + 1, '"');
				if (!close)
					break;
				*close = 0;
				strcpy_s(names[count++], MAX_PATH, p + 1);
				p = strchr(close + 1, '"');
			}
			break;
		}
		fclose(f);
		return count;
	}

	void LoadPack(const char* name)
	{
		char dir[MAX_PATH] = {};
		char rel[MAX_PATH] = {};
		sprintf_s(rel, "_HD3_Data\\Packs\\%s\\", name);
		GameRelative(dir, sizeof(dir), rel);

		char pattern[MAX_PATH] = {};
		sprintf_s(pattern, "%s*.dll", dir);
		WIN32_FIND_DATAA fd = {};
		HANDLE find = FindFirstFileA(pattern, &fd);
		if (find == INVALID_HANDLE_VALUE)
		{
			return;
		}

		do
		{
			if (!HookSiteIsStock(fd.cFileName))
				continue;

			char path[MAX_PATH] = {};
			sprintf_s(path, "%s%s", dir, fd.cFileName);
			HMODULE h = LoadLibraryA(path);
			char buf[MAX_PATH + 96];
			if (h)
				sprintf_s(buf, "[HotaLoader] loaded %s from %s (h=%p)", name, path, (void*)h);
			else
				sprintf_s(buf, "[HotaLoader] FAILED to load %s, GetLastError=%lu", path, GetLastError());
			Log(buf);
		} while (FindNextFileA(find, &fd));
		FindClose(find);
	}

	DWORD WINAPI LoadPacksThread(LPVOID)
	{
		if (!WaitForGameWindow())
		{
			Log("[HotaLoader] no game window in this process, loading nothing");
			return 0;
		}

		char names[32][MAX_PATH] = {};
		const int count = ReadPackList(names, 32);
		for (int i = 0; i < count; ++i)
			LoadPack(names[i]);
		return 0;
	}

	bool IsHostExe(const char* name)
	{
		char exe[MAX_PATH] = {};
		GetModuleFileNameA(nullptr, exe, MAX_PATH);
		const char* leaf = strrchr(exe, '\\');
		return _stricmp(leaf ? leaf + 1 : exe, name) == 0;
	}

	bool TryPatchLauncher()
	{
		HMODULE native = GetModuleHandleA("HD_LauncherNative.dll");
		if (!native)
			return false;
		Log(PatchHdLauncher(native, Log)
			? "[HotaLoader] launcher patched: plugin list enabled for HotA"
			: "[HotaLoader] launcher NOT patched: HD_LauncherNative.dll does not match");
		return true;
	}

	DWORD WINAPI PatchLauncherThread(LPVOID)
	{
		for (int i = 0; i < 10000; ++i)
		{
			if (TryPatchLauncher())
				return 0;
			Sleep(1);
		}
		Log("[HotaLoader] HD_LauncherNative.dll never loaded");
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		if (IsHostExe("HD_Launcher.exe"))
		{
			if (TryPatchLauncher())
				return TRUE;
			HANDLE t = CreateThread(nullptr, 0, PatchLauncherThread, nullptr, 0, nullptr);
			if (t)
				CloseHandle(t);
			return TRUE;
		}

		if (IsHostExe("h3hota HD.exe") || IsHostExe("h3hota.exe"))
		{
			char gameDir[MAX_PATH] = {};
			GameRelative(gameDir, sizeof(gameDir), "");
			char names[32][MAX_PATH] = {};
			BuildExtLod(gameDir, names, ReadPackList(names, 32), Log);
		}

		HANDLE t = CreateThread(nullptr, 0, LoadPacksThread, nullptr, 0, nullptr);
		if (t)
			CloseHandle(t);
	}
	return TRUE;
}
