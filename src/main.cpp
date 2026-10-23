// QBMouseFix.asi entry point.
//
// The game executable is wrapped in SteamStub: its code is still encrypted when ASI plugins are loaded.
// So at load time we only redirect two imports in the exe's (unencrypted) IAT. The first call through
// either one comes from decrypted game code; that's when the code hooks are installed, exactly once.

#include "config.h"
#include "iat.h"
#include "latency.h"
#include "log.h"
#include "mousefix.h"
#include "rawinput.h"
#include "version.h"

#include <windows.h>

#include <atomic>
#include <string>

namespace {

using RegisterRawInputDevicesFn = BOOL(WINAPI*)(PCRAWINPUTDEVICE, UINT, UINT);
using EnumDisplaySettingsAFn = BOOL(WINAPI*)(LPCSTR, DWORD, DEVMODEA*);

RegisterRawInputDevicesFn g_origRegisterRawInputDevices = nullptr;
EnumDisplaySettingsAFn g_origEnumDisplaySettingsA = nullptr;
std::atomic<bool> g_installAttempted{false};

void InstallOnce(const char* trigger)
{
    if (g_installAttempted.exchange(true)) return;
    Log("Game code running (first call via %s), installing hooks", trigger);
    if (!InstallMouseFix()) {
        Log("Mouse fix NOT active; the game runs unmodified");
        return;
    }
    if (g_config.rawInput && g_config.fixEnabled) RawInputStart();
}

BOOL WINAPI HookRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size)
{
    InstallOnce("RegisterRawInputDevices");
    const BOOL result = g_origRegisterRawInputDevices(devices, count, size);
    // The game's registration replaces ours for the mouse; take it back.
    if (RawInputActive()) RawInputStart();
    return result;
}

BOOL WINAPI HookEnumDisplaySettingsA(LPCSTR device, DWORD mode, DEVMODEA* devMode)
{
    InstallOnce("EnumDisplaySettingsA");
    return g_origEnumDisplaySettingsA(device, mode, devMode);
}

std::wstring ModuleDir(HMODULE module)
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    std::wstring dir = path;
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

void Startup(HMODULE self)
{
    const std::wstring dir = ModuleDir(self);
    LoadConfig(dir + L"QBMouseFix.ini");
    if (g_config.logEnabled) LogOpen(dir + L"QBMouseFix.log");
    Log("QBMouseFix " QBMF_VERSION_STR " loaded");

    HMODULE exe = GetModuleHandleW(nullptr);
    g_origRegisterRawInputDevices = static_cast<RegisterRawInputDevicesFn>(
        HookIat(exe, "USER32.dll", "RegisterRawInputDevices", reinterpret_cast<void*>(&HookRegisterRawInputDevices)));
    g_origEnumDisplaySettingsA = static_cast<EnumDisplaySettingsAFn>(
        HookIat(exe, "USER32.dll", "EnumDisplaySettingsA", reinterpret_cast<void*>(&HookEnumDisplaySettingsA)));

    LatencyInstall();

    if (!g_origRegisterRawInputDevices && !g_origEnumDisplaySettingsA)
        Log("Neither trigger import found in the executable; nothing to do");
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Startup(module);
    }
    return TRUE;
}
