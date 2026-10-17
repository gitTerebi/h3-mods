#pragma once

#include <Plugin.hpp>
#include <H3API.hpp>

/**
 * Uncomment to show an error message when failing to link up with patcher_x86
 */
//#define SHOW_ERROR_MESSAGE_ON_FAIL

/**
 * @brief Adventure-map key remaps for HD Mod plugins.
 */
class KeyRemapPlugin : public Plugin
{
public:
	KeyRemapPlugin() : Plugin()
	{
		Status status = Init("H3.KeyRemap");
		if (status == Status::Success)
			Start();
#ifdef SHOW_ERROR_MESSAGE_ON_FAIL
		else
			ShowError("H3.KeyRemap", status);
#endif
	}

	static KeyRemapPlugin& GetPlugin();

private:
	void Start();

public:
	int __stdcall RemapAdventureKeys(HiHook* h, h3::H3AdventureManager* This, h3::H3Msg* msg, int a3, int a4, int a5);
	int __stdcall FilterRawInput(HiHook* h, h3::H3InputManager* This, int* a2);
	SHORT __stdcall FilterGetKeyState(HiHook* h, int virtual_key);
	SHORT __stdcall FilterGetAsyncKeyState(HiHook* h, int virtual_key);
	BOOL __stdcall FilterGetKeyboardState(HiHook* h, PBYTE key_state);

private:
	bool HandleKey(h3::H3AdventureManager& adventure, h3::H3Msg& msg) const;
	bool HandleWasdRawInput(h3::H3AdventureManager& adventure, h3::H3InputManager::InputMessages& msg) const;
	void ProcessHeldWasd(h3::H3AdventureManager& adventure) const;
	void ScrollMap(h3::H3AdventureManager& adventure, int dx, int dy) const;
};
