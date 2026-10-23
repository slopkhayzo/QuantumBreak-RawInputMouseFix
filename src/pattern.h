#pragma once
#include <cstdint>
#include <windows.h>

// Finds `pattern` ("8B 81 ?? 02 ...") in the executable section(s) of `module`.
// Returns the match only if it is unique; otherwise nullptr. `matches` receives the match count.
uint8_t* FindUniquePattern(HMODULE module, const char* pattern, int* matches = nullptr);
