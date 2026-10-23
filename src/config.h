#pragma once
#include <string>

struct Config {
    // [MouseFix]
    bool fixEnabled = true;     // add unconsumed latched mouse deltas to the next latch, so every feed of a frame counts
    bool carryWheel = true;     // also carry unconsumed wheel notches
    int maxCarryMs = 100;       // older pending deltas are dropped (pause menus, loading) to avoid camera jumps
    int toggleKey = 0x77;       // VK code to toggle the whole fix at runtime (default F8); 0 disables

    // [RawInput]
    bool rawInput = true;       // dedicated raw mouse reader thread feeding the game at every input latch

    // [Latency]
    int maxFrameLatency = 1;    // DXGI max queued frames (game leaves DX11 default 3); 0 = don't touch
    int latencyToggleKey = 0x78; // VK code to toggle frame latency at runtime (default F9); 0 disables

    // [Sensitivity] multipliers on the game's mouse look speed (on top of the in-game sensitivity setting)
    float sensGlobal = 1.0f;
    float sensX = 1.0f, sensY = 1.0f;        // normal camera / hip fire
    float aimSensX = 1.0f, aimSensY = 1.0f;  // while aiming (see aimDetection)
    int aimDetection = 1;                    // 0 = zoom only, 1 = aim key held + zoomed, 2 = aim key only
    int aimKey = 0x02;                       // VK code of the aim key (default right mouse button)
    float aimZoomThreshold = 0.99f;          // camera zoom below this counts as zoomed (1.0 = not zoomed)
    float aimExitZoomRise = 0.02f;           // zoom-only mode: aiming ends once zoom rises this far above its minimum
    bool aimBlend = true;                    // ramp the aim multipliers in with zoom progress
    bool aimBlendOut = true;                 // ... and back out with the zoom-out after aiming ends
    float aimFullZoom = 0.35f;               // zoom value of a completed zoom-in (aim multipliers fully applied)
    int reloadKey = 0x79;                    // VK code that re-reads [Sensitivity] from the ini (default F10)

    // [Log]
    bool logEnabled = true;
    bool logStats = false;      // per-interval input statistics and the readAnalog diagnostic hook (development)
    int logIntervalMs = 1000;
};

extern Config g_config;

void LoadConfig(const std::wstring& iniPath);

// Re-reads only the [Sensitivity] section from the ini passed to LoadConfig.
void ReloadSensitivity();
