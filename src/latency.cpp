#include "latency.h"

#include "config.h"
#include "iat.h"
#include "log.h"

#include <d3d11.h>
#include <dxgi.h>

#include <atomic>

namespace {

using CreateDeviceFn = decltype(&D3D11CreateDevice);
using CreateDeviceAndSwapChainFn = decltype(&D3D11CreateDeviceAndSwapChain);

CreateDeviceFn g_origCreateDevice = nullptr;
CreateDeviceAndSwapChainFn g_origCreateDeviceAndSwapChain = nullptr;

std::atomic<IDXGIDevice1*> g_device{nullptr};
UINT g_gameLatency = 3;
std::atomic<bool> g_applied{false};

void Apply(ID3D11Device* device)
{
    if (!device || g_config.maxFrameLatency <= 0) return;
    IDXGIDevice1* dxgi = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice1), reinterpret_cast<void**>(&dxgi)))) {
        Log("Latency: IDXGIDevice1 not available; frame latency unchanged");
        return;
    }
    UINT before = 0;
    dxgi->GetMaximumFrameLatency(&before);
    const HRESULT hr = dxgi->SetMaximumFrameLatency(static_cast<UINT>(g_config.maxFrameLatency));
    Log("Latency: max frame latency %u -> %d (%s)", before, g_config.maxFrameLatency, SUCCEEDED(hr) ? "ok" : "failed");
    if (FAILED(hr)) {
        dxgi->Release();
        return;
    }

    g_gameLatency = before;
    g_applied.store(true);
    // Keep the newest device (the game may recreate it); the reference keeps toggling safe.
    if (IDXGIDevice1* old = g_device.exchange(dxgi)) old->Release();
}

HRESULT WINAPI HookCreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                const D3D_FEATURE_LEVEL* levels, UINT numLevels, UINT sdk, ID3D11Device** device,
                                D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context)
{
    const HRESULT hr = g_origCreateDevice(adapter, type, software, flags, levels, numLevels, sdk, device, level, context);
    if (SUCCEEDED(hr) && device) Apply(*device);
    return hr;
}

HRESULT WINAPI HookCreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                            const D3D_FEATURE_LEVEL* levels, UINT numLevels, UINT sdk,
                                            const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapChain,
                                            ID3D11Device** device, D3D_FEATURE_LEVEL* level,
                                            ID3D11DeviceContext** context)
{
    const HRESULT hr = g_origCreateDeviceAndSwapChain(adapter, type, software, flags, levels, numLevels, sdk, desc,
                                                      swapChain, device, level, context);
    if (SUCCEEDED(hr) && device) Apply(*device);
    return hr;
}

} // namespace

void LatencyInstall()
{
    if (g_config.maxFrameLatency <= 0) return;
    HMODULE renderer = GetModuleHandleW(L"d3d_x64_f.dll");
    if (!renderer) {
        Log("Latency: d3d_x64_f.dll not loaded; frame latency unchanged");
        return;
    }
    g_origCreateDevice = static_cast<CreateDeviceFn>(
        HookIat(renderer, "d3d11.dll", "D3D11CreateDevice", reinterpret_cast<void*>(&HookCreateDevice)));
    g_origCreateDeviceAndSwapChain = static_cast<CreateDeviceAndSwapChainFn>(HookIat(
        renderer, "d3d11.dll", "D3D11CreateDeviceAndSwapChain", reinterpret_cast<void*>(&HookCreateDeviceAndSwapChain)));
    if (!g_origCreateDevice && !g_origCreateDeviceAndSwapChain)
        Log("Latency: D3D11 device creation imports not found; frame latency unchanged");
}

void LatencyToggle()
{
    IDXGIDevice1* dxgi = g_device.load();
    if (!dxgi) return;
    const bool on = !g_applied.load();
    const UINT value = on ? static_cast<UINT>(g_config.maxFrameLatency) : g_gameLatency;
    const HRESULT hr = dxgi->SetMaximumFrameLatency(value);
    if (SUCCEEDED(hr)) g_applied.store(on);
    Log("Toggle key: max frame latency %u (%s)", value, on ? "fix" : "game default");
}
