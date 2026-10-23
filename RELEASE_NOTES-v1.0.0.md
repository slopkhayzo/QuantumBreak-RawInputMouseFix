# Quantum Break Raw Input Mouse Fix v1.0.0

First release.

## What it does

- **Smooth mouse look.** The game only receives raw mouse input (`WM_INPUT`)
  when its window thread processes messages, about 115 times per second and
  out of step with the frame rate. With a 1000 Hz mouse at ~180 FPS, only about
  85 of every 180 frames got any camera rotation; the rest got none and the
  next got double, which is the stepped feel. The fix reads the mouse on its
  own thread at the mouse's full polling rate, and hands the game everything
  received each time it reads input. Every frame gets camera rotation and no
  mouse movement is lost. Mouse buttons and the wheel go through the same path.
- **Lower input latency.** The game never sets DXGI's frame latency, so DirectX
  11's default of 3 queued frames applies. The fix sets it to 1
  (`[Latency] MaxFrameLatency`); no frame-rate cost was measured.
- **Precise sensitivity.** `[Sensitivity]` adds float multipliers on top of the
  in-game setting (which only has 10% steps): `Global`, `X`, `Y`, and
  `AimX`/`AimY` while aiming. Aiming is detected from the aim key and the
  camera zoom (`AimDetection`, `AimKey`; zoom-only mode for toggle aim). The
  aim values fade in and out with the zoom, matching the game's own aim
  slowdown. F10 re-reads the section while playing.
- **A/B hotkeys.** F8 turns the mouse fix off and on, F9 switches the frame
  latency between the fix's value and the game's.

## Compatibility

Tested on the Steam DirectX 11 build (`dx11\QuantumBreak.exe`, SHA-256
`2E3BAF80750F8462469FEE270B6054496BDA73DE541BB19E72AD1EA26DE4715B`). There is
no executable hash check. Each hook site is found by its byte pattern and
checked once when the game starts; a site that doesn't match is skipped and
logged, and the game runs unmodified. The Microsoft Store (DirectX 12) version
is not supported.

## Release archives

- `QuantumBreak-RawInputMouseFix-v1.0.0-plugin-only.zip`: the ASI,
  configuration, project license, third-party notices (MinHook is compiled into
  the ASI), documentation, and payload hashes, for installations that already
  have an x64 ASI loader.
- `QuantumBreak-RawInputMouseFix-v1.0.0-with-Ultimate-ASI-Loader-v9.7.4.zip`:
  the same files plus Ultimate ASI Loader x64 v9.7.4 as `winmm.dll`.

Each archive contains `SHA256SUMS.txt`; each `.zip` has a `.zip.sha256` beside
it.
