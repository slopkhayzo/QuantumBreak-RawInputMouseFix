# Quantum Break Raw Input Mouse Fix

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested a couple hours of gameplay and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue)
> this patch currently probably only works for the latest Steam version of the game.
> If you're interested and want more (human generated) info, I have a blog post [here](https://slop-blog.enkhayzomachines.net/posts/quantum-break-raw-input-mouse-fix) :)

An ASI plugin for the PC version of Quantum Break that fixes the stepped,
low-rate feel of turning the camera with the mouse, lowers input latency, and
adds precise mouse sensitivity settings.

Current release: **v1.0.0**

## What it fixes

- **Stepped mouse look.** The game registers for raw mouse input, but it only
  receives `WM_INPUT` when its window thread processes messages, about 115
  times per second and out of step with the frame rate. At ~180 FPS only
  about half of the frames got any camera rotation; the rest got none and the
  next got double. Keyboard movement was unaffected, which is why strafing
  looked smoother than turning. The plugin reads the mouse on a dedicated
  thread at the mouse's full polling rate and feeds every report to the game
  each time it reads input, so the camera turns on every frame.
- **Input latency.** The game never sets DXGI's maximum frame latency, so up to
  3 frames can be queued ahead of the GPU. The plugin sets it to 1.
- **Sensitivity.** The in-game setting only has 10% steps. The plugin adds float
  multipliers: global, horizontal and vertical, and separate values while
  aiming that fade in and out with the game's aim zoom.

## Compatibility

| Build | `QuantumBreak.exe` SHA-256 | Status |
| --- | --- | --- |
| Steam, DirectX 11 (`dx11` folder, October 2016) | `2E3BAF80750F8462469FEE270B6054496BDA73DE541BB19E72AD1EA26DE4715B` | Tested |
| Microsoft Store / Xbox app, DirectX 12 | | Not supported |

The hash identifies the tested build; it is not checked. The executable is
wrapped in SteamStub DRM, so its code is still encrypted when ASI plugins load.
The plugin therefore only redirects two imports at load time
(`RegisterRawInputDevices`, `EnumDisplaySettingsA`); the first call through
either one comes from decrypted game code, and that's when it installs its code
hooks. Each hook site is found by its byte pattern and checked once. A site
that doesn't match is skipped and logged, and the game runs unmodified.

## Install

Download a release archive:

- **with Ultimate ASI Loader**: copy `winmm.dll`, `QBMouseFix.asi` and
  `QBMouseFix.ini` into `...\steamapps\common\QuantumBreak\dx11\`.
- **plugin only**: if you already have an x64 ASI loader for the game, copy only
  `QBMouseFix.asi` and `QBMouseFix.ini`.

Launch the game normally. Everything is enabled by default. `README.txt` in the
archive covers installation, hotkeys and uninstallation; every setting is
described in `QBMouseFix.ini`.

| Key | Effect (only while the game is focused) |
| --- | --- |
| F8 | Mouse fix off/on (off = the game's original mouse input) |
| F9 | Frame latency: the fix's value (1) / the game's (3) |
| F10 | Re-read `[Sensitivity]` from the ini |

## How it works

| Part | Where | What |
| --- | --- | --- |
| Raw input reader | `src/rawinput.cpp` | A high-priority thread with a message-only window takes the process' mouse raw-input registration (`RIDEV_INPUTSINK`; the keyboard stays with the game) and accumulates every report. |
| Input latch hook | `src/mousefix.cpp` | Right before the game's keyboard/mouse device latches its per-pass mouse delta, the plugin delivers everything the reader received through the device's own raw-input handler. Deltas latched on passes without a world update are carried into the next latch, because the game reads look input from a per-frame peak-hold table that would otherwise keep only one of several feeds. |
| Mouse aim hook | `src/mousefix.cpp` | Scales the yaw/pitch speed of the game's mouse-aim function by the `[Sensitivity]` multipliers, blended by the camera zoom it already reads. |
| Frame latency | `src/latency.cpp` | Import hooks on the renderer's `D3D11CreateDevice(AndSwapChain)` call `IDXGIDevice1::SetMaximumFrameLatency`. |
| Bootstrap | `src/main.cpp` | The SteamStub-safe deferred install described above. |

## Build

Install Visual Studio 2022 or newer with the **Desktop development with C++**
workload, then run:

```bat
build.cmd
```

It configures and builds `build\Release\QBMouseFix.asi` with CMake (the copy
bundled with Visual Studio is used). The CRT is linked statically, so no
Visual C++ redistributable is needed. MinHook 1.3.4 is vendored in
`third_party/minhook`.

To make the release archives in `dist\` (the version comes from
`CMakeLists.txt`):

```powershell
.\package-release.ps1 -LoaderPath <path to Ultimate ASI Loader v9.7.4 NoPDB x64 dinput8.dll>
```

Without `-LoaderPath` only the plugin-only archive is made.

## License

MIT, see [LICENSE](LICENSE). Third-party components: MinHook (BSD 2-Clause) and,
in the convenience archive, Ultimate ASI Loader (MIT); see
[THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt).

## Misc

For more game fixes, visit: https://slop-blog.enkhayzomachines.net/fixes :)
