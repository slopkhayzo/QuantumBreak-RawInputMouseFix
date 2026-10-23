#pragma once

// DXGI frame latency control. The game never calls IDXGIDevice1::SetMaximumFrameLatency, so DX11's default of
// 3 queued frames applies; when GPU-bound that queue adds up to ~3 frames of input lag.
// Hooks d3d_x64_f.dll's D3D11CreateDevice(AndSwapChain) imports (that DLL isn't SteamStub-protected, so this can
// be done at plugin load) and applies [Latency] MaxFrameLatency to the created device.
void LatencyInstall();

// Toggles between the configured latency and the game's original value (toggle key, main thread).
void LatencyToggle();
