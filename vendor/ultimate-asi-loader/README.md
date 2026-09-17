# Ultimate ASI Loader (vendored)

Bundled copy of Ultimate ASI Loader, the install-time source of truth.
install.cmd extracts directly from here and never reaches out to the network.
Refresh manually with `pixi run update-deps`, then commit.

## Snapshot

- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- Tag: `v9.7.4`
- Commit: `6b440669144c4a0bef5718ab155df160d231cd42`
- Asset: `Ultimate-ASI-Loader_x64.zip`
- dinput8.dll SHA-256: `fa266e3513d02c08a1b808f28c10538a489eaffaa4b0707f7cc1066e71b5afd7`
- Fetched at: 2026-09-16T19:29:22.9662424+01:00

`dinput8.dll` is extracted from the upstream asset untouched; the zip itself is a
download intermediate and is not kept. The loader picks the system library it
forwards to from its own filename at run time, so install.cmd copies this one
binary into the game directory as `winmm.dll` - the import TheGreatCircle.exe
already has, and one Windows resolves from the game directory rather than from
System32.
