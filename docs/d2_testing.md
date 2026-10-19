# Dishonored 2 Void Gate: testing without a human

Three layers, from cheapest to closest to the real thing.

## 1. Offline: portal math + Monte Carlo (no game)

```
native_d2\build.cmd test [cases] [seed]
```

Builds `native_d2/tests/test_portal.exe` from `tests/test_portal.c` and runs it. It exercises `native_d2/vg_portal.h`
(the engine-free portal math the module uses) on random gate pairs (walls, floors, ceilings, slanted) and random
points, velocities and views: frames orthonormal, A->B->A identity, speed kept, "into A" = "out of B", wall-to-wall
yaw = the old hand-written rule, exit capsule in front of B and inside the oval, see-through camera behind B and
seeing through B, surface maps onto surface. Plus a frame-by-frame fall simulation (30-144 fps, the module's
smoothed speed estimate) for the floor-gate trigger. Exit code = failed checks.

## 2. The game's own console and the module's test channel

`tools/d2ctl.py` talks to the module inside a running game through two files in the game folder:
`dmod_cmd.txt` (lines `<id> <command>`, written via .tmp + rename) and `dmod_reply.txt` (`<id> <json>`).
The module's window thread reads requests every 50 ms, also in menus; commands that change the world run in the
next game frame (player physics update hook). The channel is OFF unless a file `dmod_dev.txt` is in the game folder
(checked once per module load; the log says "test channel on/off"), so players' games never take commands from a
file. The dev machine has the marker; the setup neither creates nor removes it.

```
py tools\d2ctl.py ping | state | cmds | npcs <r> | log <text> | console <text>
py tools\d2ctl.py tp x y z [yaw pitch] | gate i x y z nx ny nz | clear | vel x y z | vz v | comp <typehex> [bytes]
py tools\d2ctl.py shot [file.png]        screenshot of the game window
py tools\d2ctl.py press <vk>             focus the game and press a key (0x20 = space)
```

- `console <text>` appends to the engine's command buffer (idCmdSystemLocal at 0x1424F6920, slot 9
  AppendCommandText). Retail still has 113 commands (`cmds` lists them), among them `LoadGame [slot]`, `SaveGame`,
  `map`, `restartmap`, `screenshot`, `setviewpos`, `teleport`, `killEntity`, `damage`, `trigger`, and cvars work
  too. `win_pauseOnFocusLoss 0` keeps the game running while another window has the focus (set by `d2test load`).
  `god` / `buddha` only flip cvars nothing reads in retail.
- `state`: feet / eye / fwd, `vel` (physics velocity: [GetComponent(player, 0x143C28CF0, 2) + 0x48] + 0x48, real
  while airborne, zeroed by the ground movement), `vz` (the module's estimate), `fallHeight` (the falling state's
  accumulated height), fall protection, gates.
- `comp 143C064A0 48`: the player's arkCpntHealthEvolved; current health = float at +0x20 (max at +0x114).
- Entities: `ents <r>` (id, class, position, velocity), `ent <id>`, `entp <id> x y z`, `rm <id>`,
  `spawndef x y z dx dy dz <entityDef>` (any def at a point, facing a direction; e.g.
  `models/powers/windblast/windblast_object.def` blows what is in front of it like a cast), `newents <ms>` (log
  every entity appearing within 15 m of the player, with its def name), `entrylog 0|1` (the floor-gate entry test
  per call).
- See-through window: `viewon`, `viewclip 0|1|2`, `viewclipoff`, `viewrect`, `viewout`, `viewfit` (the polygon's
  corners as a fraction of the oval, 0.93), `viewumbra`; `frames` (module view submits so far + time: the frame rate,
  scenario `d2test.py fps`).
- NPCs: `npcs <r>` (id, def name, position, +0xD0 flags: byte +0xD1 0x08 = dead, 0x20 = unconscious), `npcfall`,
  `npcsoft 0|1` (soft landing after a gate), `npcmode`. `ray x y z dx dy dz`: distance to the first hit and to the
  first solid static world (clear heights for drop tests: atrium x 0.7, y 27..30 is clear from the floor at 20 up to 33).

## 3. Scenarios in the game: `tools/d2test.py`

```
py tools\d2test.py load 33           LoadGame 33 (test save), wait, press space past "press to start", no pause on focus loss
py tools\d2test.py fall [speed]      floor gate hit at <speed> m/s -> floor exit upwards -> land: health kept
py tools\d2test.py fling [speed]     floor gate -> wall gate: horizontal flight past the exit, health kept
py tools\d2test.py mc [n] [seed]     Monte Carlo: random floating gate pairs and entries (heights, speeds, offsets)
```

More scenarios: `select` (power wheel: Void Gate at 12 o'clock), `aim` (frames of the aim marker while aiming),
`walk` (W held into a wall gate), `view` / `clip` / `surfaces` / `shape` (see-through window; screenshots in
scratch/), `fps`. Slot 33 quick keys: 1 = pistol, 6 / 8 = Blink, 9 = Windblast (greater + shockwave upgrades); fire / cast
with the right mouse button (`d2ctl.mouse("right", ...)`).

While the user plays, the game window loses focus and the game opens its pause menu (whatever
`win_pauseOnFocusLoss` says): test only when the user is not playing.

Careful in the atrium: gunshots, Windblast and flying tanks alert the guards near (1, 49); a few seconds later they
come and kill the player (game over: reload slot 33). Do noisy tests right after a load, and reload before the
regression runs. In the background the game updates the player's origin only ~11-30 times a second (the tick runs
3-4 times per update): fast-fall tests are near the limit there.
Mouse and keys go to the game through `d2ctl.press` / `d2ctl.mouse` (the game window is focused first).

They work around the spot the player stands on (flat floor, a few meters free around and ~8 m above). The one
used so far: Addermire Institute atrium, `tp 0.7 15.1 20.05 90 0`. Save slot 33 is the test save made there (Corvo, Void Gate selected, nothing carried, no gates); slot 31 is the user's own. Not the
rocks at (1.5, 10): that is shallow sea, the player drowns there after ~17 s.

Data mods (game closed): ALWAYS all four, in this order -- d2install rebuilds the voidmod catalogs from exactly the
mods given, installing only VoidGate removes the user's Boost / NonLethal / NormalBoltSleeps:
```
py tools\d2install.py install d2mods\NormalBoltSleeps d2mods\Boost d2mods\NonLethal d2mods\VoidGate
```

Typical unattended loop:
```
powershell -File tools\d2run.ps1            start the game (main menu)
py tools\d2test.py load 33
py tools\d2ctl.py tp 0.7 15.1 20.05 90 0
py tools\d2test.py mc 30 1
powershell -File tools\d2run.ps1 -Module     after a module change: hot reload, then rerun
```

Ghidra: `tools\gdec.cmd serve` (once, in the background) keeps the project open; then `py tools\gdec.py dec|xrefs|
name|vt|find ...` answer in well under a second, and several callers can share it.

## 4. Release build and the player setup

```
py tools\make_sig.py                 after changing game addresses in host.c / vg.c: native_d2\vg_sig.h
python tools\vg_build.py             build.cmd + release\payload + release\VoidGateSetup.exe + release\VoidGate-<ver>.zip
python tools\vg_build.py --no-exe    only the payload (for the offline setup test)
```

`VoidGateSetup.exe [game folder] [--lang=en|ru]` opens the setup window (tools/vg_setup_gui.py: Welcome -> Game
folder -> Installing -> Done; Russian on a Russian Windows, else English; restarts itself elevated via UAC when the
game folder is not writable, `--go=install|uninstall` then continues where it was). For scripts and tests:
`python tools\vg_setup.py [game folder] install|uninstall [--yes]` (the console path; the exe has no console).
Scenario tests of the window on fake game folders: scratch scripts test_gui.py / click_uninstall.py (all pages,
a real install + uninstall, the uninstaller exe pressing its buttons and deleting itself). It layers Void Gate on top of
the current `gameN_voidmod` catalogs (other mods are carried over byte for byte), keeps the state before the first
install in `<game>\base\voidgate_backup` (with the hashes of what it wrote, so a later Void Installer run is
noticed), copies `dinput8.dll` / `voidgate.dll` and `VoidGate_Uninstall.exe` (a copy of the setup that only
uninstalls, then deletes itself). The 21 stock resources in mod.json "copy" entries are read from the player's own
game and are not shipped.

Checked on 2026-10-19: an offline test on fake game folders (install over three mods == `d2install` of all four,
reinstall, install after another tool rewrote the catalogs, byte-exact uninstall, stock install/uninstall); the
real game: install over NormalBoltSleeps + Boost + NonLethal, the `fall` scenario passed, `VoidGate_Uninstall.exe`
restored the catalogs byte for byte and removed itself; save slot 33 (Void Gate bought and selected) loads and the
power wheel works with the Void Gate data removed. After such tests reinstall the dev state with `d2install` (all
four mods) and copy the dev DLLs back.
