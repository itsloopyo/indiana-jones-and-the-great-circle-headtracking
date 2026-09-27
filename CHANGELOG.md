# Changelog

## [Unreleased]

### Added

- Head tracking for Indiana Jones and the Great Circle on the Xbox Game Pass /
  Microsoft Store build. Your head moves the view while the mouse or controller
  keeps the aim, with yaw, pitch, roll and positional lean. On any other build
  the mod stays dormant and the game runs unmodified.
- World markers and the weapon reticle follow the head-tracked view, so an
  interaction prompt stays on the thing it points at and the reticle follows the
  aim direction as you turn your head.
- Head tracking is scaled to the field of view the game is drawing. Raising a
  weapon narrows the view, which used to make the same head movement sweep
  further across the screen; it now covers the same distance whether you are
  walking around or aiming. Head roll is left alone, and the game's own Field of
  View slider is followed on the next frame, with no restart.
- The game window is centred on its monitor at startup when the game is running
  windowed and has not centred it itself. The game places its window twice while
  it starts, and both placements are centred, so the window does not jump.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed
- Settings move to `CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is `CompensateWorldMarkers=0`: world markers and the weapon reticle always follow the head-tracked view now, so a file that turned that off is imported with it on, and the log names the line.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. A nav-cluster code and its chord letter from `HeadTracking.ini` (`ToggleKey` and `ChordToggleKey`, for example) become one list, and `CycleModeKey` becomes `CycleTrackingModeKey`.
- The other settings keep their meaning under the fleet's names: `[Rotation] LocalSmoothing` and `RemoteSmoothing` move to `[Smoothing]`, `[Position] Enabled` becomes the startup tracking mode `RotationEnabled` / `PositionEnabled`, `LimitX`, `LimitZ` and `LimitZBack` become `PositionLimitX`, `PositionLimitZ` and `PositionLimitZBack`, and `LimitY`, which bounded the lean down as well as up, becomes both `PositionLimitY` and `PositionLimitYDown`.
- Cycling the tracking mode (`Page Up` / `Ctrl+Shift+G`) and switching the yaw mode (`Page Down` / `Ctrl+Shift+H`) now save the new mode to `CameraUnlock.ini`, so the game starts in it next time. Turning head tracking on or off with `End` still changes the current session only; whether it starts on is `EnableOnStartup`.
- A value in `CameraUnlock.ini` the mod cannot use keeps its default and the log names the line; nothing is clamped. The position limits take any value from 0 to 10 metres, where `HeadTracking.ini` pulled anything above 0.5 back to 0.5, and `UdpPort` takes 1 to 65535, where `HeadTracking.ini` took 1024 to 65535.
- `uninstall.cmd` keeps `CameraUnlock.ini` and `HeadTracking.ini`; it used to delete `HeadTracking.ini`. Nothing installs, seeds or ships either file.

### Removed
- `[General] CompensateWorldMarkers`, which could turn off the correction that keeps world markers and the weapon reticle on the head-tracked view. The correction is always on.
