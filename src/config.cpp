#include "config.h"

#include <windows.h>

Config g_config;

static int ReadInt(const std::wstring& ini, const wchar_t* section, const wchar_t* key, int def)
{
    // GetPrivateProfileInt only understands decimal; accept 0x-prefixed hex for VK codes too.
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 32, ini.c_str());
    if (!buf[0]) return def;
    return static_cast<int>(wcstol(buf, nullptr, 0));
}

static std::wstring g_iniPath;

static float ReadFloat(const std::wstring& ini, const wchar_t* section, const wchar_t* key, float def)
{
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(section, key, L"", buf, 32, ini.c_str());
    wchar_t* end = nullptr;
    const double v = wcstod(buf, &end);
    return end != buf ? static_cast<float>(v) : def;
}

static void LoadSensitivity(const std::wstring& ini)
{
    Config& c = g_config;
    c.sensGlobal = ReadFloat(ini, L"Sensitivity", L"Global", 1.0f);
    c.sensX = ReadFloat(ini, L"Sensitivity", L"X", 1.0f);
    c.sensY = ReadFloat(ini, L"Sensitivity", L"Y", 1.0f);
    c.aimSensX = ReadFloat(ini, L"Sensitivity", L"AimX", 1.0f);
    c.aimSensY = ReadFloat(ini, L"Sensitivity", L"AimY", 1.0f);
    c.aimZoomThreshold = ReadFloat(ini, L"Sensitivity", L"AimZoomThreshold", 0.99f);
    c.aimExitZoomRise = ReadFloat(ini, L"Sensitivity", L"AimExitZoomRise", 0.02f);
    c.aimDetection = ReadInt(ini, L"Sensitivity", L"AimDetection", 1);
    if (c.aimDetection < 0 || c.aimDetection > 2) c.aimDetection = 1;
    c.aimKey = ReadInt(ini, L"Sensitivity", L"AimKey", 0x02);
    c.aimBlend = ReadInt(ini, L"Sensitivity", L"AimBlend", 1) != 0;
    c.aimBlendOut = ReadInt(ini, L"Sensitivity", L"AimBlendOut", 1) != 0;
    c.aimFullZoom = ReadFloat(ini, L"Sensitivity", L"AimFullZoom", 0.35f);
}

void ReloadSensitivity()
{
    if (!g_iniPath.empty()) LoadSensitivity(g_iniPath);
}

void LoadConfig(const std::wstring& ini)
{
    g_iniPath = ini;
    LoadSensitivity(ini);
    Config& c = g_config;
    c.reloadKey = ReadInt(ini, L"Sensitivity", L"ReloadKey", c.reloadKey);
    c.fixEnabled = ReadInt(ini, L"MouseFix", L"Enabled", c.fixEnabled) != 0;
    c.carryWheel = ReadInt(ini, L"MouseFix", L"CarryWheel", c.carryWheel) != 0;
    c.maxCarryMs = ReadInt(ini, L"MouseFix", L"MaxCarryMs", c.maxCarryMs);
    c.toggleKey = ReadInt(ini, L"MouseFix", L"ToggleKey", c.toggleKey);
    c.rawInput = ReadInt(ini, L"RawInput", L"Enabled", c.rawInput) != 0;
    c.maxFrameLatency = ReadInt(ini, L"Latency", L"MaxFrameLatency", c.maxFrameLatency);
    c.latencyToggleKey = ReadInt(ini, L"Latency", L"ToggleKey", c.latencyToggleKey);
    if (c.maxFrameLatency > 16) c.maxFrameLatency = 16;
    c.logEnabled = ReadInt(ini, L"Log", L"Enabled", c.logEnabled) != 0;
    c.logStats = ReadInt(ini, L"Log", L"Stats", c.logStats) != 0;
    c.logIntervalMs = ReadInt(ini, L"Log", L"IntervalMs", c.logIntervalMs);
    if (c.maxCarryMs < 0) c.maxCarryMs = 0;
    if (c.logIntervalMs < 100) c.logIntervalMs = 100;
}
