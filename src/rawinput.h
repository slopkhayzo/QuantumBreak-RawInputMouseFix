#pragma once
#include <cstdint>

// Dedicated raw mouse reader. The game only receives WM_INPUT when its window thread pumps messages
// (~115 times/s), so 1000 Hz mouse data reaches the game in bursts and many frames get no camera movement.
// This thread owns the process' raw mouse registration, receives every report as it arrives, and hands the
// data to the game's own raw-input handler right before each input latch (on the game's main thread).

// Starts the reader thread (once) and (re)registers the mouse to it. Call after the game registered its devices.
void RawInputStart();

// Called on the game's main thread right before the KBM device latch: feeds all mouse data received since the
// previous call through the device's raw-input handler. No-op until the reader is running.
void RawInputFeed(uint8_t* device);

bool RawInputActive();

// Hands the mouse back to the game's original registration (false) or claims it again (true). Used by the toggle key.
void RawInputSetEnabled(bool enabled);

struct RawInputStats {
    int64_t reports;     // WM_INPUT mouse reports received by the reader thread
    int64_t feeds;       // RawInputFeed calls that delivered movement
    int64_t buttons;     // button/wheel reports delivered
    int64_t unfocused;   // reports discarded because the game wasn't the foreground window
};
RawInputStats RawInputTakeStats();
