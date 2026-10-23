#pragma once

// Locates the hook sites (one-time signature check each) and installs the hooks.
// Must run after SteamStub has decrypted the executable's code. Returns true if the core hooks are active.
bool InstallMouseFix();

// True once the core hooks (input latch + world update) are active.
bool MouseFixInstalled();
