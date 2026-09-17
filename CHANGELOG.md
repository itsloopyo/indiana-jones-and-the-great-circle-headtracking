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
