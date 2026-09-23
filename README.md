# Indiana Jones and the Great Circle Head Tracking

![Indiana Jones and the Great Circle running with this mod](https://raw.githubusercontent.com/itsloopyo/indiana-jones-and-the-great-circle-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Indiana Jones and the Great Circle that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view, your mouse or controller keeps the aim
- **6DOF tracking** - yaw, pitch and roll, plus positional lean, peek and duck
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- A purchased copy of Indiana Jones and the Great Circle **on Xbox Game Pass or the Microsoft Store**. That is the only build this mod works on - see [Supported builds](#supported-builds) before installing.
- A tracking source: [OpenTrack](https://github.com/opentrack/opentrack/releases), or any app that sends the OpenTrack UDP protocol
- 64-bit Windows 10 or 11

The installer bundles the x64 [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases), so you do not need to download it separately.

### Supported builds

**Xbox Game Pass / Microsoft Store only.** That is the one copy this mod has
been built against and the one copy it has been tested in, and it is the only
build it will do anything at all on.

The mod identifies the build it is running in from the executable's PE header
and only engages on one it has offsets for. Anything else leaves every hook
uninstalled, so the game runs exactly as it would without the mod and
`HeadTracking.log` says which way the build differs. That is a deliberate
failsafe, not a bug: the addresses this mod writes through are pinned to one
exact executable, and using them against a different one would crash the game
seconds in.

| Store | Build | Status |
|-------|-------|--------|
| Xbox Game Pass / Microsoft Store | `gdk-win64-20260527` (package 1.16.1.0) | Tested, works |
| Steam | - | **Not supported.** Never built against, never tested. The mod stays dormant and the game runs unmodified. |

Steam is not a matter of flipping a switch. It is a separate binary with its own
build date and its own addresses, so it needs its own profile derived against a
copy of it - and there is no Steam copy to hand. If you own it on Steam, this
mod will do nothing for you today.

## Installation

### Lopari

[Lopari](https://lopari.app) does not list this mod yet. Once this mod is
available in Lopari, download [Lopari](https://lopari.app), choose
**Indiana Jones and the Great Circle**, and click **Play with head tracking**.

### Standalone Installer

There is no published release yet. Once there is one, the installer ZIP is the
whole of it:

1. Download the installer ZIP from the Releases page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your game, point it at the folder yourself, either
with an environment variable or with the path as the first argument:

```powershell
$env:INDIANA_JONES_AND_THE_GREAT_CIRCLE_PATH = 'D:\XboxGames\Indiana Jones and the Great Circle\Content'
.\install.cmd
```

```powershell
.\install.cmd "D:\XboxGames\Indiana Jones and the Great Circle\Content"
```

The folder to give it is the one holding `TheGreatCircle.exe`, which on
Xbox Game Pass is `<drive>:\XboxGames\Indiana Jones and the Great Circle\Content`.

If you also own the game on Steam, do not point the installer at that copy. It
will happily copy the files in and the mod will then sit dormant, because the
Steam executable is not a build it recognises.

### Manual Installation

Both files go beside `TheGreatCircle.exe`, in the folder named above:

1. Copy `vendor/ultimate-asi-loader/dinput8.dll` into the game folder and rename
   it to `winmm.dll`. That executable already imports `winmm.dll`, and the loader
   picks the system library it forwards to from its own filename, so the rename
   is all that is needed.
2. Copy `GreatCircleHeadTracking.asi` into the same folder.
3. Launch the game. The mod writes `HeadTracking.ini` next to the executable on
   first run, and logs to `HeadTracking.log` beside it.

Mod managers do not deploy this mod, and there is no Nexus download. Both files
have to sit beside `TheGreatCircle.exe`, and a manager deploys into one fixed
subtree below the game folder rather than into the folder itself.

## Setting Up OpenTrack

- **Input**: whichever tracker you are using
- **Output**: `UDP over network`
- **Output options**: host `127.0.0.1`, port `4242`
- Map yaw, pitch and roll, plus X, Y and Z if you want positional tracking
- Press **Start**, then launch the game

Centering is done in your tracker: OpenTrack's Center bind, your phone app's own
center button, or SteamVR's reset.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on `UDP over network`, host `127.0.0.1`, port `4242`.

### Webcam Setup

1. Set OpenTrack's **Input** to `neuralnet tracker`, which needs no markers and
   no IR hardware.
2. Pick your webcam in the tracker options, and set the resolution and frame rate
   it runs at.
3. Leave **Output** on `UDP over network`, host `127.0.0.1`, port `4242`.

### Phone App Setup

This mod accepts one thing: the OpenTrack UDP protocol on port `4242`. A phone
app is usable here if it sends that protocol itself, or ships a PC-side
companion that does.

For an app that does send it, what decides the wiring is how much filtering it
does before the packet leaves the phone:

- **An app that filters on-device can send straight to the PC.** Point it at this
  PC's LAN address on port `4242`. I made [Headcam](https://headcam.app) so
  decent tracking was free for anybody with a phone already in their pocket; it
  filters on-device, so it can send directly. Any app that filters enough noise
  works the same way.
- **A raw or lightly filtered feed goes through OpenTrack.** Send it to OpenTrack
  as the input and let OpenTrack's filters and curves clean it up, with
  OpenTrack's own output going to `127.0.0.1:4242`. Take this route as well if
  you want OpenTrack's curve mapping.

The test is quicker than the reading: try direct, hold your head still, and if
the view drifts or shakes, route it through OpenTrack instead.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a
tracker running on this same PC that sends to this PC's LAN address instead of
`127.0.0.1` - the mod sees a transport, not a machine.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches head yaw between world-locked and
camera-local.

All six keys are remappable in `HeadTracking.ini`.

## Configuration

`HeadTracking.ini` is written next to `TheGreatCircle.exe` on first launch and
documents every key it accepts:

```ini
; Indiana Jones and the Great Circle Head Tracking - configuration
; Edit values, restart the game to apply.
;
; Controls (all remappable, see [Hotkeys]):
;           End  / Ctrl+Shift+Y   toggle tracking
;           PgUp / Ctrl+Shift+G   cycle tracking mode (rotation and position
;                                 / rotation only / position only)
;           PgDn / Ctrl+Shift+H   yaw about world up / about the view axis
;
; There is no recenter key. Centre your head in your tracker (OpenTrack's
; Center bind, or your phone app's CENTER button) - this mod uses the pose it
; is sent, exactly as sent, so one centre anywhere is the whole story.
;
; Field of view is a game setting, not a mod setting. The game has its own
; Field of View slider under Options > Video. This mod rotates and moves
; the camera and never writes its field of view.

[Network]
; The port your tracker sends OpenTrack UDP packets to.
UdpPort=4242

[General]
EnableOnStartup=1
; 1 = head yaw turns about world up, so a glance left stays level while you
; are looking up or down a stairwell. 0 = yaw turns about the view axis.
WorldSpaceYaw=1
; Keep world markers aligned with the tracked view and rotate the reticle
; with the game aim direction. Reticle lean parallax is not corrected.
CompensateWorldMarkers=1

[Hotkeys]
; Windows virtual key codes, in hex. Each action has a nav-cluster key and a
; Ctrl+Shift+<key> chord, and both fire it - remap either or both.
; Common codes: End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21, PgDn 0x22,
; F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.
ToggleKey=0x23
CycleModeKey=0x21
YawModeKey=0x22
ChordToggleKey=0x59
ChordCycleModeKey=0x47
ChordYawModeKey=0x48

; Head movement is used exactly as your tracker sends it. There is no
; sensitivity, deadzone or axis inversion here on purpose: set those in
; OpenTrack or your phone app once, and every game behaves the same way.

[Rotation]
; Smoothing covers rotation and position alike, and the value used is picked
; per connection from where the tracker sends from. 0.0 none .. 1.0 heavy.
; LOCAL means the packets arrive from 127.0.0.1. A tracker running on this
; same PC that sends to this PC's LAN address counts as REMOTE - the mod sees
; a transport, not a machine.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
; Whether positional lean is on when the game starts. The PgUp / Ctrl+Shift+G
; cycle changes it afterwards, so this chooses the launch mode rather than
; locking it.
Enabled=1
; How far the view may lean from where the game put it, in metres.
; 0 to 0.50 on each axis. A value past either end is pulled back to it and the
; log says so; 0 is a real setting and pins that axis.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10
```

Field of view stays a game setting, under **Options -> Video -> Field of View**.
The mod reads it every frame and scales head yaw, pitch and lean by how far the
game has narrowed the view, so raising a weapon does not exaggerate head
tracking. Move the slider and the mod follows it on the next frame, with no
restart.

### Window placement

If you play windowed, the mod centers the game window on the work area of the
monitor the game puts it on. A fullscreen or borderless window, one that is
already centered, and one too large to fit are all left where they are, as is a
window you move yourself. The `[window]` lines in `HeadTracking.log` say what
happened. There is no setting for it.

## Troubleshooting

`HeadTracking.log`, next to `TheGreatCircle.exe`, is where every answer below
starts. The previous run's copy is kept beside it as `HeadTracking.prev.log`,
which is the one to read after a crash.

**Mod not loading**

- Check that `winmm.dll` and `GreatCircleHeadTracking.asi` are both in the same
  folder as `TheGreatCircle.exe`, not in a subfolder.
- No `HeadTracking.log` next to the executable means the loader never loaded the
  mod. Re-run `install.cmd`, or place both files by hand as above.
- A log line saying the build is not recognised means the mod is dormant on this
  build and the game is running unmodified. Either it is the Steam copy, which
  is not supported at all, or the Xbox Game Pass build has been patched since
  `gdk-win64-20260527` and needs a new profile.

**No tracking response**

- Check OpenTrack is running, **Start** is pressed, and its output is
  `UDP over network` to `127.0.0.1:4242`.
- Press `End` or `Ctrl+Shift+Y`. Tracking may be toggled off.
- If `HeadTracking.log` says the UDP port could not be bound, leave the game
  running. The mod re-attempts the bind every 500ms, so closing whatever else
  holds the port brings tracking back within half a second, with no relaunch.
  `Bound UDP port 4242 after Ns of waiting - tracking is live` is the line that
  says it worked, and the `[receiver]` line quotes the reason Windows gave for
  each refusal.

**Jittery / unstable tracking**

- A phone, or any other sender on the network, gets `RemoteSmoothing` (0.15 by
  default). Raise it toward 1.0 in `HeadTracking.ini` for a noisy link.
- Clean the feed at the source: better lighting for a webcam, and OpenTrack's
  own filter stage for a raw phone feed.
- A tracker on this PC that sends to this PC's LAN address counts as remote.
  Send to `127.0.0.1` to get `LocalSmoothing` instead.

**Wrong rotation axis**

- Yaw feeling wrong when you look far up or down is the yaw mode. Press
  `Page Down` or `Ctrl+Shift+H` to switch between world-locked (the default,
  horizon-stable) and camera-local.
- An axis that moves the wrong way is a tracker setting. Fix the mapping or
  inversion in OpenTrack or your phone app, so one profile behaves the same way
  in every game.

**Known limitations**

- Tracking stays live on the main menu, so the menu background moves with your
  head. It is suppressed while the game is loading and whenever game time is
  stopped - the pause menu, inventory, alt-tab - so this is the main menu only.
  Press `End` or `Ctrl+Shift+Y` there if it bothers you.
- Leaning is not stopped by walls. The limits keep the view within arm's reach
  of where the game put the camera, but nothing checks what is in the way, so
  leaning hard into a doorframe can put the view through it. Lean less, or press
  `Page Up` / `Ctrl+Shift+G` to run rotation only.
- The reticle follows your aim as you turn your head, but not as you lean. Under
  a sideways or forward lean it sits slightly off where the shot lands, by more
  at close range than far.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes `GreatCircleHeadTracking.asi`,
`HeadTracking.ini`, `HeadTracking.log` and `HeadTracking.prev.log`, so keep a
copy of the ini first if you have tuned it. The mod loader is only removed if the
installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires [pixi](https://pixi.sh), CMake, and Visual Studio 2022 or newer with the
Desktop development with C++ workload.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/indiana-jones-and-the-great-circle-headtracking.git
cd indiana-jones-and-the-great-circle-headtracking
pixi run build
pixi run test
pixi run package
```

`pixi run install` builds and deploys into every detected game install.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. Third-party components are
recorded in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- **MachineGames** and **Bethesda Softworks** - Indiana Jones and the Great Circle, published under licence from Lucasfilm Ltd.
- **[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)** by ThirteenAG - the proxy DLL that loads the mod
- **[OpenTrack](https://github.com/opentrack/opentrack)** - head tracking input
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu - function hooking

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by MachineGames,
Bethesda Softworks, ZeniMax Media or Lucasfilm Ltd. Indiana Jones and Lucasfilm
are trademarks of Lucasfilm Ltd.; MachineGames, Bethesda, Bethesda Softworks and
ZeniMax are trademarks of ZeniMax Media Inc. They are named here only to
identify the game this mod is for. Use at your own risk.
