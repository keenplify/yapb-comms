# Agent instructions

## Natural bot turning

Bots must not perform gratuitous full-circle spins while navigating or changing
idle look direction. Keep turning purposeful and smooth.

- Always use the shortest signed angular difference across the +/-180-degree
  heading boundary. Never add or subtract 360 degrees to force a longer arc.
- Use `navigationTurnStep` in `inc/navigation_aim.h` for navigation turning.
  Discard momentum pointing away from a new aim point, bound angular speed, and
  stop at the aim point without overshooting. Keep yaw normalized.
- Keep idle aim offsets small and change them gradually; never randomize a new
  heading every frame. Disable idle drift during combat and precise actions.
- Preserve the existing combat aiming controller when fixing navigation turns.
  Do not change recoil, movement physics, or combat accuracy as a side effect.
- Add regression coverage for heading-boundary crossings, target reversals,
  stopping at the aim point, and slow-frame updates when changing turn logic.
  Run the standalone behavior tests and build the 32-bit Linux module.
- Verify route corners and transitions between navigation, idle scanning, and
  combat in-game when a test server is available. A successful build alone
  does not establish that all visible spinning has been eliminated.

## User-owned game configuration

Never read, write, create, replace, append to, rename, delete, repair, seed,
back up, restore, or otherwise manipulate `config.cfg`. Use launcher-owned
files or normal game/server interfaces instead.
