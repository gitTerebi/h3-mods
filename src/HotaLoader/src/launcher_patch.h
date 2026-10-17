#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

typedef void (*LogFn)(const char* msg);

bool PatchHdLauncher(HMODULE native, LogFn log);
