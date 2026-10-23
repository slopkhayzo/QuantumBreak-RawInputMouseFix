#pragma once
#include <string>

void LogOpen(const std::wstring& path);
void Log(const char* fmt, ...);
