#include "rawinput.h"

#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstring>

namespace {

constexpr UINT kMsgReregister = WM_APP + 1;
constexpr UINT kMsgRelease = WM_APP + 2;
constexpr uint32_t kRawInputEventType = 0x10;  // app:: event id for raw input (event = u32 type + RAWINPUT)
constexpr size_t kKbmRawHandlerSlot = 7;       // KBM device vtable: handleAppEvent(RawInputEvent*)

// Button/wheel reports, single producer (reader thread) / single consumer (game main thread).
struct ButtonReport {
    USHORT flags;
    USHORT data;
};
constexpr uint32_t kRingSize = 1024;
ButtonReport g_ring[kRingSize];
std::atomic<uint32_t> g_ringHead{0}, g_ringTail{0};

std::atomic<int32_t> g_dx{0}, g_dy{0};
std::atomic<bool> g_active{false};
HWND g_hwnd = nullptr;
HANDLE g_ready = nullptr;

std::atomic<int64_t> g_reports{0}, g_feeds{0}, g_buttons{0}, g_unfocused{0};

bool Register()
{
    RAWINPUTDEVICE rid{0x01, 0x02, RIDEV_INPUTSINK, g_hwnd};  // generic desktop / mouse
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        Log("RawInput: RegisterRawInputDevices failed (%lu)", GetLastError());
        return false;
    }
    return true;
}

void OnInput(HRAWINPUT handle)
{
    RAWINPUT ri;
    UINT size = sizeof(ri);
    if (GetRawInputData(handle, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    if (ri.header.dwType != RIM_TYPEMOUSE) return;

    const RAWMOUSE& m = ri.data.mouse;
    g_reports.fetch_add(1, std::memory_order_relaxed);
    // The game ignores MOUSE_MOVE_ABSOLUTE reports (tablets, RDP); keep that behavior.
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE) && (m.lLastX || m.lLastY)) {
        g_dx.fetch_add(m.lLastX, std::memory_order_relaxed);
        g_dy.fetch_add(m.lLastY, std::memory_order_relaxed);
    }
    if (m.usButtonFlags) {
        const uint32_t head = g_ringHead.load(std::memory_order_relaxed);
        if (head - g_ringTail.load(std::memory_order_acquire) < kRingSize) {
            g_ring[head % kRingSize] = {m.usButtonFlags, m.usButtonData};
            g_ringHead.store(head + 1, std::memory_order_release);
        }
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INPUT) {
        OnInput(reinterpret_cast<HRAWINPUT>(lp));
    } else if (msg == kMsgReregister) {
        const bool ok = Register();
        g_active.store(ok);
        return ok;
    } else if (msg == kMsgRelease) {
        // Same registration the game makes itself: mouse, no flags, follows the focus window.
        g_active.store(false);
        RAWINPUTDEVICE rid{0x01, 0x02, 0, nullptr};
        return RegisterRawInputDevices(&rid, 1, sizeof(rid));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

DWORD WINAPI ReaderThread(LPVOID)
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"QBMouseFixRawInput";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);

    const bool ok = g_hwnd && Register();
    g_active.store(ok);
    SetEvent(g_ready);
    if (!ok) {
        Log("RawInput: reader thread failed to start; the game keeps its own mouse input");
        return 0;
    }
    Log("RawInput: reader thread running, mouse registered to it");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
    return 0;
}

bool GameIsForeground()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

using RawHandlerFn = bool(__fastcall*)(uint8_t* device, uint8_t* event);

void Deliver(RawHandlerFn handler, uint8_t* device, LONG dx, LONG dy, USHORT buttonFlags, USHORT buttonData)
{
    RAWINPUT ri{};
    ri.header.dwType = RIM_TYPEMOUSE;
    ri.header.dwSize = sizeof(RAWINPUT);
    ri.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
    ri.data.mouse.usButtonFlags = buttonFlags;
    ri.data.mouse.usButtonData = buttonData;
    ri.data.mouse.lLastX = dx;
    ri.data.mouse.lLastY = dy;

    alignas(8) uint8_t event[sizeof(uint32_t) + sizeof(RAWINPUT)];
    std::memcpy(event, &kRawInputEventType, sizeof(uint32_t));
    std::memcpy(event + sizeof(uint32_t), &ri, sizeof(ri));
    handler(device, event);
}

} // namespace

void RawInputStart()
{
    if (g_hwnd) {
        // The game (re)registered its devices, which took the mouse back: claim it again.
        RawInputSetEnabled(true);
        return;
    }
    g_ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (HANDLE thread = CreateThread(nullptr, 0, ReaderThread, nullptr, 0, nullptr)) {
        WaitForSingleObject(g_ready, 2000);
        CloseHandle(thread);
    }
}

void RawInputSetEnabled(bool enabled)
{
    if (!g_hwnd) return;
    DWORD_PTR result = 0;
    SendMessageTimeoutW(g_hwnd, enabled ? kMsgReregister : kMsgRelease, 0, 0, SMTO_BLOCK, 1000, &result);
    g_dx.store(0);
    g_dy.store(0);
    g_ringTail.store(g_ringHead.load());
}

bool RawInputActive() { return g_active.load(std::memory_order_relaxed); }

void RawInputFeed(uint8_t* device)
{
    if (!g_active.load(std::memory_order_relaxed)) return;

    const LONG dx = g_dx.exchange(0, std::memory_order_relaxed);
    const LONG dy = g_dy.exchange(0, std::memory_order_relaxed);
    uint32_t tail = g_ringTail.load(std::memory_order_relaxed);
    const uint32_t head = g_ringHead.load(std::memory_order_acquire);
    if (!dx && !dy && tail == head) return;

    if (!GameIsForeground()) {
        g_ringTail.store(head, std::memory_order_release);
        g_unfocused.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    auto handler = reinterpret_cast<RawHandlerFn>((*reinterpret_cast<void***>(device))[kKbmRawHandlerSlot]);
    if (dx || dy) {
        Deliver(handler, device, dx, dy, 0, 0);
        g_feeds.fetch_add(1, std::memory_order_relaxed);
    }
    for (; tail != head; ++tail) {
        const ButtonReport& b = g_ring[tail % kRingSize];
        Deliver(handler, device, 0, 0, b.flags, b.data);
        g_buttons.fetch_add(1, std::memory_order_relaxed);
    }
    g_ringTail.store(tail, std::memory_order_release);
}

RawInputStats RawInputTakeStats()
{
    return {g_reports.exchange(0), g_feeds.exchange(0), g_buttons.exchange(0), g_unfocused.exchange(0)};
}
