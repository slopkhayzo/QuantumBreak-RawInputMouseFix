#pragma once
#include <windows.h>

// Replaces `module`'s import of `dll!func` with `hook`. Returns the previous pointer, or nullptr if not imported.
void* HookIat(HMODULE module, const char* dll, const char* func, void* hook);
