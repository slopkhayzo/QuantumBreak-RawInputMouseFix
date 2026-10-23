// Mouse-look fix for Quantum Break (Steam DX11 build).
//
// Engine pipeline (see research notes):
//   WM_INPUT -> app_x64_f event FIFO -> KBM device: acc{X,Y,W} += raw deltas
//   main loop (Sleep(0) spin) -> input::update -> Latch(): d{X,Y,W} = acc; acc = 0     <- every loop pass
//   ... world/aim update runs only when the renderer is idle; it reads look input from a per-frame peak-hold table.
// The game only receives WM_INPUT when its window thread pumps messages (~115 Hz), so many frames got no mouse
// movement at all (stepped camera). The raw reader (rawinput.cpp) feeds every report right before each latch.
//
// Carry (F1): several feeds land in one frame, and the peak-hold table would keep only the largest one, so an
// unconsumed latch is added to the next one instead of being replaced.
//
// Sensitivity: the mouse aim apply (yaw/pitch from look input) is scaled by the [Sensitivity] multipliers.

#include "mousefix.h"

#include "config.h"
#include "latency.h"
#include "log.h"
#include "pattern.h"
#include "rawinput.h"

#include <MinHook.h>
#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

// KBM device field offsets (device object at [0x14122a5b8] in the 2016-10-20 Steam build).
constexpr size_t kAccX = 0x210, kAccY = 0x214, kAccW = 0x218;
constexpr size_t kDX = 0x21c, kDY = 0x220, kDW = 0x224;

// KBM device latch: copies acc -> d, clears acc, ages key edge flags.
constexpr char kSigLatch[] =
    "8B 81 10 02 00 00 48 8B D1 89 81 1C 02 00 00 41 B8 02 00 00 00 8B 81 14 02 00 00 89 81 20 02 00 00 8B 81 18 02 00 00 89";
// World update (runs the aim/camera input components once per world step).
constexpr char kSigWorldUpdate[] =
    "48 83 EC 68 48 C7 44 24 20 FE FF FF FF 48 8B 05 ?? ?? ?? ?? 48 83 78 28 00 74 2B 48 8B C8 E8 ?? ?? ?? ?? 48 85 C0 74 17";
// Mouse aim apply: yaw/pitch delta from look input (both aim paths call it).
constexpr char kSigMouseAimApply[] =
    "48 89 5C 24 10 57 48 81 EC 80 00 00 00 0F 29 74 24 70 48 8B D9 48 8B 0D ?? ?? ?? ?? 48 8B FA F3 0F 10 35 ?? ?? ?? ?? 0F";
// Diagnostics only: input::readAnalog(action, bool).
constexpr char kSigReadAnalog[] =
    "8B 05 ?? ?? ?? ?? 83 F8 02 74 19 83 F8 04 0F 85 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 63 C9 F3 0F";
constexpr int kActionLookX = 7, kActionLookY = 8;

using LatchFn = void(__fastcall*)(uint8_t* device);
using WorldUpdateFn = void(__fastcall*)(void* self);
using ReadAnalogFn = float(__fastcall*)(int action, bool flag);
using MouseAimApplyFn = void(__fastcall*)(uint8_t* self, const float* input, float speedX, float speedY, float dt);
using ZoomFn = float(__fastcall*)(void* cameraManager);

LatchFn g_origLatch = nullptr;
WorldUpdateFn g_origWorldUpdate = nullptr;
ReadAnalogFn g_origReadAnalog = nullptr;
MouseAimApplyFn g_origMouseAimApply = nullptr;
ZoomFn g_getZoom = nullptr;              // camera zoom getter that the mouse aim apply itself calls
void* const* g_cameraManager = nullptr;  // global the apply passes to it
const int* g_readMode = nullptr;  // input read mode global (1 live, 2/4 peak-hold tables), from readAnalog's first insn
uintptr_t g_exeBase = 0;

std::atomic<uint8_t*> g_device{nullptr};
std::atomic<bool> g_consumed{true};       // latched deltas were read by a world update
std::atomic<int64_t> g_pendingSince{0};   // QPC time the current unconsumed window started
std::atomic<bool> g_fixEnabled{true};
std::atomic<bool> g_installed{false};

int64_t g_qpcFreq = 1;

// Diagnostics, reset every log interval. Relaxed atomics: latch and world update may run on different threads.
struct Stats {
    std::atomic<int64_t> latches, nonZeroLatches, worldUpdates, doubleConsumes;
    std::atomic<int64_t> rawX, rawY, rawW;               // sum of |acc| moved by each latch (all mouse input)
    std::atomic<int64_t> usedX, usedY;                   // sum of |d| seen by world updates (live value, diag only)
    std::atomic<int64_t> droppedLatches, droppedCounts;  // unconsumed non-zero latches overwritten
    std::atomic<int64_t> carriedLatches, carriedCounts;  // unconsumed non-zero latches carried by the fix
    std::atomic<int64_t> maxSpins, spinsSinceWorld;      // latches between world updates
    std::atomic<int64_t> maxWorldGapUs, lastWorldQpc;
    std::atomic<int64_t> lookReadsByMode[5];             // readAnalog(look X/Y) calls per read mode (0..4)
    std::atomic<int64_t> aimCalls, aimingCalls;          // mouse aim apply calls, and those counted as aiming
    std::atomic<int64_t> aimInMilli, aimOutMicroRad;     // |input| x1000, |yaw|+|pitch| x1e6
    std::atomic<int64_t> zoomMinMilli{INT64_MAX}, zoomMaxMilli{INT64_MIN};
} g_stats;

// Per-caller readAnalog(look X/Y) totals: who consumes mouse look, and how much.
struct CallerSlot {
    std::atomic<uintptr_t> rva{0};
    std::atomic<int64_t> calls{0}, nonZero{0}, sumMilli{0};
};
CallerSlot g_lookCallers[16];

CallerSlot* CallerFor(uintptr_t rva)
{
    for (CallerSlot& slot : g_lookCallers) {
        uintptr_t cur = slot.rva.load(std::memory_order_relaxed);
        if (cur == rva) return &slot;
        if (!cur && slot.rva.compare_exchange_strong(cur, rva)) return &slot;
        if (cur == rva) return &slot;
    }
    return nullptr;
}

int64_t Qpc()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

int32_t& Field(uint8_t* device, size_t offset) { return *reinterpret_cast<int32_t*>(device + offset); }

void UpdateMax(std::atomic<int64_t>& target, int64_t value)
{
    int64_t cur = target.load(std::memory_order_relaxed);
    while (value > cur && !target.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {}
}

void UpdateMin(std::atomic<int64_t>& target, int64_t value)
{
    int64_t cur = target.load(std::memory_order_relaxed);
    while (value < cur && !target.compare_exchange_weak(cur, value, std::memory_order_relaxed)) {}
}

int64_t Take(std::atomic<int64_t>& v) { return v.exchange(0, std::memory_order_relaxed); }

void LogSensitivity(const char* prefix)
{
    const Config& c = g_config;
    static const char* const kModes[] = {"zoom", "key+zoom", "key"};
    Log("%s: global=%.4f x=%.4f y=%.4f aimX=%.4f aimY=%.4f | aimDetection=%s aimKey=0x%X aimZoomThreshold=%.3f "
        "aimExitZoomRise=%.3f aimBlend=%d aimBlendOut=%d aimFullZoom=%.3f%s",
        prefix, c.sensGlobal, c.sensX, c.sensY, c.aimSensX, c.aimSensY, kModes[c.aimDetection], c.aimKey,
        c.aimZoomThreshold, c.aimExitZoomRise, c.aimBlend, c.aimBlendOut, c.aimFullZoom,
        !g_origMouseAimApply                     ? " (INACTIVE: hook skipped)"
        : !g_getZoom && c.aimDetection != 2      ? " (aim values unused: no zoom getter)"
                                                 : "");
}

// Edge-detects a hotkey; only while the game is the foreground window.
bool Pressed(int vk, bool& wasDown, bool foreground)
{
    const bool down = vk && foreground && (GetAsyncKeyState(vk) & 0x8000);
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

void PollHotkeys(int64_t now)
{
    static int64_t lastPoll = 0;
    static bool fixWasDown = false, latencyWasDown = false, reloadWasDown = false;
    if (now - lastPoll < g_qpcFreq / 30) return;
    lastPoll = now;

    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool foreground = pid == GetCurrentProcessId();
    if (Pressed(g_config.latencyToggleKey, latencyWasDown, foreground)) LatencyToggle();
    if (Pressed(g_config.reloadKey, reloadWasDown, foreground)) {
        ReloadSensitivity();
        LogSensitivity("Reload key: sensitivity");
    }
    if (Pressed(g_config.toggleKey, fixWasDown, foreground)) {
        const bool on = !g_fixEnabled.load();
        g_fixEnabled.store(on);
        if (g_config.rawInput) {
            if (on) RawInputStart();
            else RawInputSetEnabled(false);
        }
        Log("Toggle key: fix %s", on ? "ON" : "OFF");
    }
}

void FlushStats(int64_t now)
{
    static int64_t lastFlush = 0;
    if (!g_config.logEnabled || !g_config.logStats) return;
    if (!lastFlush) { lastFlush = now; return; }
    const int64_t elapsed = now - lastFlush;
    if (elapsed < g_qpcFreq * g_config.logIntervalMs / 1000) return;
    lastFlush = now;

    Stats& s = g_stats;
    const int64_t rawX = Take(s.rawX), rawY = Take(s.rawY), usedX = Take(s.usedX), usedY = Take(s.usedY);
    const int64_t raw = rawX + rawY, used = usedX + usedY;
    Log("fix=%d raw=%d latch=%lld(nz %lld) world=%lld dbl=%lld spinsMax=%lld gapMaxMs=%.2f | raw=%lld(x%lld y%lld w%lld) "
        "used=%lld(%.1f%%) dropped=%lld/%lldcnt carried=%lld/%lldcnt | %.0fms",
        g_fixEnabled.load() ? 1 : 0, RawInputActive() ? 1 : 0, Take(s.latches), Take(s.nonZeroLatches),
        Take(s.worldUpdates), Take(s.doubleConsumes), Take(s.maxSpins), Take(s.maxWorldGapUs) / 1000.0, raw, rawX, rawY,
        Take(s.rawW), used, raw ? 100.0 * used / raw : 100.0, Take(s.droppedLatches), Take(s.droppedCounts),
        Take(s.carriedLatches), Take(s.carriedCounts), 1000.0 * elapsed / g_qpcFreq);

    if (g_config.rawInput) {
        const RawInputStats r = RawInputTakeStats();
        Log("  raw reader: reports=%lld feeds=%lld buttons=%lld unfocused=%lld", r.reports, r.feeds, r.buttons,
            r.unfocused);
    }
    if (g_origReadAnalog) {
        char callers[1024];
        int len = 0;
        for (CallerSlot& slot : g_lookCallers) {
            const uintptr_t rva = slot.rva.load(std::memory_order_relaxed);
            if (!rva) break;
            const int64_t calls = Take(slot.calls), nonZero = Take(slot.nonZero), sum = Take(slot.sumMilli);
            if (!calls || len > 900) continue;
            len += snprintf(callers + len, sizeof(callers) - len, " [%llx n=%lld nz=%lld sum=%.3f]",
                            static_cast<unsigned long long>(rva), calls, nonZero, sum / 1000.0);
        }
        callers[len] = 0;
        Log("  look reads by mode: m1=%lld m2=%lld m4=%lld other=%lld |%s", Take(s.lookReadsByMode[1]),
            Take(s.lookReadsByMode[2]), Take(s.lookReadsByMode[4]),
            Take(s.lookReadsByMode[0]) + Take(s.lookReadsByMode[3]), callers);
    }
    if (g_origMouseAimApply) {
        const int64_t zMin = s.zoomMinMilli.exchange(INT64_MAX), zMax = s.zoomMaxMilli.exchange(INT64_MIN);
        Log("  mouse aim apply: calls=%lld aiming=%lld zoom=[%.3f..%.3f] sum|in|=%.3f sum|out|=%.4f rad",
            Take(s.aimCalls), Take(s.aimingCalls), zMin == INT64_MAX ? 0.0 : zMin / 1000.0,
            zMax == INT64_MIN ? 0.0 : zMax / 1000.0, Take(s.aimInMilli) / 1000.0, Take(s.aimOutMicroRad) / 1e6);
    }
}

float __fastcall HookReadAnalog(int action, bool flag)
{
    const float value = g_origReadAnalog(action, flag);
    if (action == kActionLookX || action == kActionLookY) {
        const int mode = g_readMode ? *g_readMode : 0;
        g_stats.lookReadsByMode[(mode >= 0 && mode <= 4) ? mode : 0].fetch_add(1, std::memory_order_relaxed);
        const uintptr_t rva = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_exeBase;
        if (CallerSlot* slot = CallerFor(rva)) {
            slot->calls.fetch_add(1, std::memory_order_relaxed);
            if (value != 0.0f) slot->nonZero.fetch_add(1, std::memory_order_relaxed);
            slot->sumMilli.fetch_add(static_cast<int64_t>(std::fabs(value) * 1000.0f), std::memory_order_relaxed);
        }
    }
    return value;
}

// Zoom-only aim detection. The camera zoom drops quickly when aiming starts but eases back over ~2 s after it ends,
// so a plain threshold releases late. Instead: aiming starts while the zoom is decreasing below the threshold and
// ends as soon as it rises more than aimExitZoomRise above the lowest zoom seen while aiming.
bool AimingFromZoom(float zoom)
{
    static bool aiming = false;
    static float lastZoom = 1.0f, minZoom = 1.0f;
    const Config& c = g_config;
    if (zoom >= c.aimZoomThreshold) {
        aiming = false;
    } else if (!aiming) {
        if (zoom < lastZoom - 1e-4f) {
            aiming = true;
            minZoom = zoom;
        }
    } else {
        if (zoom < minZoom) minZoom = zoom;
        if (zoom > minZoom + c.aimExitZoomRise) aiming = false;
    }
    lastZoom = zoom;
    return aiming;
}

bool AimKeyHeld() { return g_config.aimKey && (GetAsyncKeyState(g_config.aimKey) & 0x8000); }

// yaw = input.x * speedX * k * zoomScale, pitch = input.y * speedY * k * zoomScale. Scaling the speed arguments
// applies the [Sensitivity] multipliers in float, after the game's (10%-step) menu sensitivity.
void __fastcall HookMouseAimApply(uint8_t* self, const float* input, float speedX, float speedY, float dt)
{
    Stats& s = g_stats;
    const Config& c = g_config;
    bool aiming = false;
    float blend = 1.0f;  // how far the aim multipliers apply (0 = hip values, 1 = aim values)
    if (g_getZoom && *g_cameraManager) {
        const float zoom = g_getZoom(*g_cameraManager);
        // The game's own aim slowdown follows the zoom (pow(zoom, k)); ramping the aim multipliers with zoom
        // progress keeps entering aim as smooth as leaving it, instead of a speed spike while the zoom catches up.
        if (c.aimBlend && c.aimFullZoom < 1.0f) {
            blend = (1.0f - zoom) / (1.0f - c.aimFullZoom);
            blend = blend < 0.0f ? 0.0f : blend > 1.0f ? 1.0f : blend;
        }
        const bool zoomAiming = AimingFromZoom(zoom);  // always run so its state stays current
        switch (c.aimDetection) {
        case 0: aiming = zoomAiming; break;
        case 2: aiming = AimKeyHeld(); break;
        default: aiming = AimKeyHeld() && zoom < c.aimZoomThreshold; break;
        }
        UpdateMin(s.zoomMinMilli, static_cast<int64_t>(zoom * 1000.0f));
        UpdateMax(s.zoomMaxMilli, static_cast<int64_t>(zoom * 1000.0f));
    } else if (c.aimDetection == 2) {
        aiming = AimKeyHeld();
    }
    if (aiming) s.aimingCalls.fetch_add(1, std::memory_order_relaxed);
    // After aiming ends the zoom eases back out; with AimBlendOut the aim values keep following it down (the same
    // blend as the zoom-in), so the speed stays continuous until the camera is back at hip zoom.
    static bool zoomTail = false;
    if (aiming) zoomTail = c.aimBlend && c.aimBlendOut && g_getZoom;  // the tail ends on zoom, so it needs the getter
    else if (blend <= 0.0f) zoomTail = false;
    const float w = (aiming || zoomTail) ? blend : 0.0f;
    speedX *= c.sensGlobal * (c.sensX + (c.aimSensX - c.sensX) * w);
    speedY *= c.sensGlobal * (c.sensY + (c.aimSensY - c.sensY) * w);

    g_origMouseAimApply(self, input, speedX, speedY, dt);

    s.aimCalls.fetch_add(1, std::memory_order_relaxed);
    s.aimInMilli.fetch_add(static_cast<int64_t>((std::fabs(input[0]) + std::fabs(input[1])) * 1000.0f),
                           std::memory_order_relaxed);
    const float yaw = *reinterpret_cast<float*>(self + 0x160), pitch = *reinterpret_cast<float*>(self + 0x164);
    s.aimOutMicroRad.fetch_add(static_cast<int64_t>((std::fabs(yaw) + std::fabs(pitch)) * 1e6f),
                               std::memory_order_relaxed);
}

void __fastcall HookLatch(uint8_t* device)
{
    g_device.store(device, std::memory_order_relaxed);
    const int64_t now = Qpc();
    RawInputFeed(device);

    const int32_t accX = Field(device, kAccX), accY = Field(device, kAccY), accW = Field(device, kAccW);
    const int32_t oldX = Field(device, kDX), oldY = Field(device, kDY), oldW = Field(device, kDW);
    const bool consumed = g_consumed.exchange(false);

    g_origLatch(device);

    Stats& s = g_stats;
    s.latches.fetch_add(1, std::memory_order_relaxed);
    if (accX || accY) s.nonZeroLatches.fetch_add(1, std::memory_order_relaxed);
    s.rawX.fetch_add(std::abs(accX), std::memory_order_relaxed);
    s.rawY.fetch_add(std::abs(accY), std::memory_order_relaxed);
    s.rawW.fetch_add(std::abs(accW), std::memory_order_relaxed);
    UpdateMax(s.maxSpins, s.spinsSinceWorld.fetch_add(1, std::memory_order_relaxed) + 1);

    if (consumed) {
        g_pendingSince.store(now, std::memory_order_relaxed);
    } else if (oldX || oldY || oldW) {
        const int64_t counts = std::abs(oldX) + std::abs(oldY);
        const int64_t age = now - g_pendingSince.load(std::memory_order_relaxed);
        if (g_fixEnabled.load(std::memory_order_relaxed) && age <= g_qpcFreq * g_config.maxCarryMs / 1000) {
            Field(device, kDX) += oldX;
            Field(device, kDY) += oldY;
            if (g_config.carryWheel) Field(device, kDW) += oldW;
            s.carriedLatches.fetch_add(1, std::memory_order_relaxed);
            s.carriedCounts.fetch_add(counts, std::memory_order_relaxed);
        } else {
            s.droppedLatches.fetch_add(1, std::memory_order_relaxed);
            s.droppedCounts.fetch_add(counts, std::memory_order_relaxed);
            g_pendingSince.store(now, std::memory_order_relaxed);
        }
    }

    PollHotkeys(now);
    FlushStats(now);
}

void __fastcall HookWorldUpdate(void* self)
{
    Stats& s = g_stats;
    if (uint8_t* device = g_device.load(std::memory_order_relaxed)) {
        if (g_consumed.load(std::memory_order_relaxed)) s.doubleConsumes.fetch_add(1, std::memory_order_relaxed);
        s.usedX.fetch_add(std::abs(Field(device, kDX)), std::memory_order_relaxed);
        s.usedY.fetch_add(std::abs(Field(device, kDY)), std::memory_order_relaxed);
    }

    const int64_t now = Qpc();
    if (const int64_t last = s.lastWorldQpc.exchange(now, std::memory_order_relaxed))
        UpdateMax(s.maxWorldGapUs, (now - last) * 1000000 / g_qpcFreq);
    s.worldUpdates.fetch_add(1, std::memory_order_relaxed);
    s.spinsSinceWorld.store(0, std::memory_order_relaxed);

    g_origWorldUpdate(self);
    g_consumed.store(true);
}

uint8_t* Find(HMODULE exe, const char* name, const char* sig)
{
    int matches = 0;
    uint8_t* target = FindUniquePattern(exe, sig, &matches);
    if (!target) Log("%s: signature matched %d times (need exactly 1) - hook skipped", name, matches);
    return target;
}

// MinHook writes the trampoline into `original` before the hook is enabled, so a detour can never observe a null
// original, even if another thread enters it right away.
template <class Fn>
bool HookAt(HMODULE exe, const char* name, uint8_t* target, Fn detour, Fn& original)
{
    const auto rva = static_cast<unsigned long long>(target - reinterpret_cast<uint8_t*>(exe));
    if (MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&original)) != MH_OK) {
        Log("%s: MinHook create failed at RVA 0x%llx - hook skipped", name, rva);
        original = nullptr;
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        Log("%s: MinHook enable failed at RVA 0x%llx - hook skipped", name, rva);
        MH_RemoveHook(target);
        original = nullptr;
        return false;
    }
    Log("%s: hooked at RVA 0x%llx", name, rva);
    return true;
}

template <class Fn>
bool Hook(HMODULE exe, const char* name, const char* sig, Fn detour, Fn& original)
{
    uint8_t* target = Find(exe, name, sig);
    return target && HookAt(exe, name, target, detour, original);
}

} // namespace

bool InstallMouseFix()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_qpcFreq = f.QuadPart;
    g_fixEnabled.store(g_config.fixEnabled);

    if (MH_Initialize() != MH_OK) {
        Log("MinHook init failed");
        return false;
    }

    HMODULE exe = GetModuleHandleW(nullptr);
    g_exeBase = reinterpret_cast<uintptr_t>(exe);

    // Both core hooks are needed: without the world-update hook the latch can't tell consumed from dropped deltas.
    if (!Hook(exe, "WorldUpdate", kSigWorldUpdate, &HookWorldUpdate, g_origWorldUpdate)) return false;
    if (!Hook(exe, "InputLatch", kSigLatch, &HookLatch, g_origLatch)) {
        MH_DisableHook(MH_ALL_HOOKS);
        return false;
    }

    // Sensitivity: optional, skipped on signature mismatch.
    if (uint8_t* apply = Find(exe, "MouseAimApply", kSigMouseAimApply)) {
        // +0x15: mov rcx, [rip+disp32] (camera manager); +0x40: call rel32 (zoom getter). Checked once, then decoded
        // before hooking.
        if (apply[0x15] == 0x48 && apply[0x16] == 0x8B && apply[0x17] == 0x0D && apply[0x40] == 0xE8) {
            g_cameraManager = reinterpret_cast<void* const*>(apply + 0x1c + *reinterpret_cast<const int32_t*>(apply + 0x18));
            g_getZoom = reinterpret_cast<ZoomFn>(apply + 0x45 + *reinterpret_cast<const int32_t*>(apply + 0x41));
        } else {
            Log("MouseAimApply: zoom getter not where expected - aim sensitivity uses the hip values");
        }
        HookAt(exe, "MouseAimApply", apply, &HookMouseAimApply, g_origMouseAimApply);
    }

    // Diagnostics ([Log] Stats=1 only): optional, skipped on signature mismatch.
    if (uint8_t* readAnalog = g_config.logStats ? Find(exe, "ReadAnalog (diag)", kSigReadAnalog) : nullptr) {
        // First insn is `mov eax, [rip+disp32]` (8B 05 disp32) reading the read-mode global.
        // Decode it before hooking: MinHook overwrites these bytes with its jump.
        g_readMode = reinterpret_cast<const int*>(readAnalog + 6 + *reinterpret_cast<const int32_t*>(readAnalog + 2));
        HookAt(exe, "ReadAnalog (diag)", readAnalog, &HookReadAnalog, g_origReadAnalog);
    }

    g_installed.store(true);
    Log("Mouse fix installed (fix %s, carryWheel=%d, maxCarryMs=%d, toggleKey=0x%X)",
        g_config.fixEnabled ? "ON" : "OFF", g_config.carryWheel, g_config.maxCarryMs, g_config.toggleKey);
    LogSensitivity("Sensitivity");
    return true;
}

bool MouseFixInstalled() { return g_installed.load(); }
