Quantum Break Raw Input Mouse Fix v@VERSION@
========================================

Fixes the stepped, low-rate feel of turning the camera with the mouse in the
PC version of Quantum Break, lowers input latency, and adds precise mouse
sensitivity settings.

What it fixes
-------------
- Stepped mouse look. The game only receives raw mouse input when its window
  thread processes messages, about 115 times per second and out of step with
  the frame rate. Many frames got no camera rotation at all and the next one
  got double, while movement with the keyboard stayed smooth. The fix reads
  the mouse on its own thread at the mouse's full polling rate and hands every
  report to the game each time it reads input, so the camera turns on every
  frame and no mouse movement is lost.
- Input latency. The game leaves DirectX 11's default of up to 3 frames
  queued ahead of the GPU. The fix lowers it to 1.
- Sensitivity. The in-game setting only has 10% steps. The fix adds exact
  multipliers: global, horizontal and vertical, and separate values while
  aiming that fade in and out with the game's aim zoom.

Supported game build
--------------------
Quantum Break on Steam, DirectX 11 version (the QuantumBreak.exe in the dx11
folder, October 2016 build). Reference QuantumBreak.exe SHA-256:
2E3BAF80750F8462469FEE270B6054496BDA73DE541BB19E72AD1EA26DE4715B

The hash identifies the tested build; it is not checked. When the game
starts, the plugin finds each place it hooks by its byte pattern and checks it
once. If a pattern does not match, that part is skipped (the log says which)
and the game runs unmodified. The Microsoft Store / Xbox app (DirectX 12)
version is not supported.

Choose an installation
----------------------
Plugin-only archive:
  Requires an x64 ASI loader that the game already loads. Copy
  QBMouseFix.asi and QBMouseFix.ini into the game's dx11 folder, next to
  QuantumBreak.exe, or into your loader's plugin folder. Keep both files
  together.

Convenience archive with Ultimate ASI Loader:
  If the game has no ASI loader, copy winmm.dll, QBMouseFix.asi, and
  QBMouseFix.ini into the game's dx11 folder:
    ...\steamapps\common\QuantumBreak\dx11\
  The bundled winmm.dll is Ultimate ASI Loader x64 v9.7.4, renamed to
  winmm.dll, one of the DLL names the game loads from its own folder.

  If the game already has an ASI loader, do not overwrite it. Copy only
  QBMouseFix.asi and QBMouseFix.ini.

For either installation, close the game before copying files, then launch it
normally from Steam.

Usage
-----
Everything is enabled by default; no key press is needed. Hotkeys, only
while the game window is focused, for comparisons:
  F8   turn the mouse fix off and on (off = the game's original mouse input)
  F9   switch the frame queue between the fix's value (1) and the game's (3)
  F10  re-read the [Sensitivity] section of QBMouseFix.ini while playing

Every option, including the keys, is described in QBMouseFix.ini.

Sensitivity: the multipliers apply on top of the in-game sensitivity setting
and default to 1.0 (unchanged). Global scales everything; X and Y apply to the
normal camera; AimX and AimY apply while aiming. The game already slows the
camera while aiming; about 3.0 makes aiming turn roughly as fast as the normal
camera. The aim values fade in and out with the camera's zoom, so the speed
changes as smoothly as the game's own aim slowdown.

Aiming is detected from the aim key (right mouse button by default, AimKey)
together with the camera zoom. If you aim with another key, set AimKey; if you
use toggle aim, set AimDetection=0 (zoom only).

Notes
-----
- The fix reads the mouse's raw input on its own thread. Mouse movement is
  ignored while the game is not the foreground window.
- Mice that report absolute positions (pen tablets, some remote-desktop
  setups) are ignored, as in the unmodified game.

Diagnostics
-----------
The plugin writes QBMouseFix.log beside the ASI: which hooks were installed
or skipped and why, and hotkey presses. Check it if the fix does not seem to
work. Set Stats=1 under [Log] in QBMouseFix.ini to also log input statistics
once per second, for bug reports.

Uninstallation
--------------
Close the game and remove QBMouseFix.asi and QBMouseFix.ini. The
QBMouseFix.log diagnostic file may also be removed.

Do not remove winmm.dll if it existed before this fix or if another plugin
depends on it. Remove the bundled loader only when you installed that exact
file for this fix and no other ASI plugin needs it.

Third-party components
----------------------
QBMouseFix.asi includes MinHook (BSD 2-Clause License). The explicitly named
convenience archive also includes Ultimate ASI Loader v9.7.4 by ThirteenAG
under the MIT License. See THIRD-PARTY-NOTICES.txt.

Misc
----
For more game fixes, visit: https://slop-blog.enkhayzomachines.net/fixes :)
