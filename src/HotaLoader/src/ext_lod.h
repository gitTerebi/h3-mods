#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "launcher_patch.h"

void BuildExtLod(const char* gameDir, char packs[][MAX_PATH], int packCount, LogFn log);
