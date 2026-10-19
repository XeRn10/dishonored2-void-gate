# Dishonored 2: a "Portal" power for Emily

Goal (2026-10-13): a new power for Emily — cast with the left hand, it places two Void portals that can be walked
through and that show the other side. Native look and feel: the game's own animations, effects and sounds,
its own rune and upgrade tree in the journal, a slot on the power wheel, icons and RU/EN texts.

Game: `D:\Dishonored 2`, GOG 1.77.9.0, `Dishonored2.exe` x64, SHA-256 `C3150F9F…60293`,
timestamp 0x5C050611 (2018-12-03). **No ASLR** (DllCharacteristics 0x8120, base 0x140000000) —
addresses are stable. No anti-cheat. Imports `DINPUT8.dll` → x64 proxy `dinput8.dll`
(`dxgi.dll` in the game folder is taken by ReShade).

Exe copy for analysis: `D:\RE\targets\d2\Dishonored2.exe`, Ghidra project `D:\RE\ghidra-projects\Dishonored2.gpr`.

---

## Status (2026-10-13, afternoon)

Details by topic: docs/d2_testing.md (tests), docs/d2_portal_render_research.md (window), d2_npc_fall_research.md,
d2_raycast_research.md. What passes through the gates (native_d2/vg.c, verified in game):
* the player (with momentum), NPCs (ragdoll when exiting into the air), bodies, items (movable 1859..1862);
* projectiles of class 1853 (`arkProjectile`): crossbow bolts, grenades, **pistol bullets** (250 m/s; caught every frame by
  `fast_projectiles`: the gate plane is crossed inside the oval within 0.1 s and the hknp ray up to it is clear). Test: a shot
  into A with B behind the player — the bullet hit the player in the back (90 -> 70);
* **Windblast**: the power spawns a wind entity (`windblast[_greater][_shockwave]_object.def`; for Corvo, slot 33 is
  greater shockwave); if a gate is in front of it (up to 8 m, 35° cone, no static world in between), an identical wind is
  spawned from the other gate, rotated through the gates (`wind_seen`). Real-cast test: an item at the exit flew 16 m;
* Blinking into a gate carries the player through (they cross the gate the same way as when falling).
Do not pass through: picked-up NPC weapons and other "generic" entities of class 1848 (they are logged).

Evening of 2026-10-13: the exit camera is derived from the whole rotated frame (forward + up, `vg_view_to_angles`):
previously yaw was taken only from the view direction and was arbitrary when exiting up/down from floor/ceiling
gates — the image turned "upside down" (reported in user testing; offline test: 40% of such exits before, 0 now).
Soft landing for NPCs after the gates (`npc_soft_landing`). There is one window, drawn only when visible; rendering
windows on both gates in turns caused flicker — reverted.

Cast (evening of 2026-10-13, `tools/make_voidgate_cast.py`): Mesmerize arm scenes instead of Far Reach (aim at a point
and release — same as the gates), own `voidgate_cast_in/loop/out.midnightscene`; the release scene gets the added event
`ARK_MDNT_MSG_GRAPPLE_CAST_BEAM` (Far Reach casts on it, and the module places the gates) — verified: gates get placed.
Mesmerize animations are in the shared arm set (`player/base/left_arm.animset`). Whether Corvo's arm is visible and
the Mesmerize sounds play cannot be checked via screenshots (the arm is not in frame even for Blink) — needs user testing.

2026-10-19, after release 0.9.0 ("the second gate is invisible", "passing through one makes the other invisible";
user testing on the Dreadful Wale, mission 8, Emily): the gate entity is fine, its FX is not. The log showed
`started 1 stopped 0, handle FFFFFFFF` within 0.3-0.9 s of the spawn for about half the gates there. Ghidra: arkCpntFX
update 0x14058E040 starts its FX once (0x14058E120: FX instance per owner entity id 0x1401FEDA0, end listener at
instance+0x128 = cpntFX+0x20, activation 0x1401FF210) and never again; the listener 0x14058E7F0 sets the handle (+0x4C)
to -1 when the FX ends. Why the FX system ends it so early (no stop from us, `m_dontSpawnIfCulled` (+0x41) is off) is
still unknown. Fix in 0.9.1: `gate_fx_watchdog` clears "started" (+0x70) of a live gate whose FX ended on its own, so
the component starts a new one (at most 8 times per gate, logged "FX ended on its own after N s: restarting it").
Verified in the user's game: the restarted FX came back (alive 1 a second later) and both gates stayed visible.
The cpntFX parameter table (0x14255B378): m_location, m_tagName, m_target, m_targetTagName, m_updatePosition,
m_dontSpawnIfCulled, m_stopOnTargetRemoval, m_stopOnParentRemoval (bools at +0x40..+0x43).

2026-10-19 (pre-release):
* Own gate aim (`aim_own_update`): a ray from the eyes, 20 m (40 m with "Far Rift"), static world only. Before, the gates
  inherited Far Reach aiming: 15 m / 6.2 m vertically in stock, and 300 / 124 m with NonLethal — hence "gates outside the
  level". The Far Reach marker for all target types = the gate preview; the module moves it to its own aim point or
  hides it. The cast places the gates using its own aim; Far Reach never pulls.
* Upgrades are now real (`upgrades_update`, checked the same way as the journal: 0x14051C010 → inventory
  `GetComponent(player, 0x143C2D850)`, `0x14072DE00(inv, itemDecl)`, entries at inv+0x38 with stride 0x38, count at +0x20):
  "Far Rift" (BLINK_BLINKATTACK) — range ×2; "Through the Void" (FARREACH_PULL_ATTACK) — NPCs, bodies, items,
  grenades; "Crossfire" (MESMERIZE_ANIMAL, item voidgate_momentum) — bolts, bullets, Windblast. Velocity
  preservation — always on (design decision). Test: `upg <mask>`, `item <name>`, `aim`.
* Texts: `tools/make_voidgate_lang.py` (EN/RU + English for the other 9 languages). Screenshots for the mod page:
  `d2mods/VoidGate/art/screens/`.

2026-10-15, based on feedback after mission 7:
* "Snapping back": the rule "1.5 s later and 10 m below the exit = fell through the world" fired on legitimate falls from
  high gates (log: exit 40 m below the floor, 7 snap-backs in a row — each put the player onto the floor entry gate). Now:
  snap back only if, 4.5 s after the exit, the player is still falling fast and has not landed (~160 m); after a snap-back
  the entry gate "sleeps" until the player steps off it (`stuckGate`). Verified: an 11 m fall — no snap-back.
* "You can get under the map" (gates where the player must not be able to go): emergency exit — F after passing, if the
  player is 5+ m below the exit point and farther than 4 m from both gates, returns them to the entry point and closes
  the gates (`interact_tap`). Verified: exit under the atrium floor, landing at z −11.4, F — back at the entry. Forbidding
  such spots at placement (rays with a "player-only walls" filter, like Blink) — open task: power filter masks come from data.
* Mission 7: the gates and the timepiece share the HUD element `ARK_HUD_ELEMENT_ITEM_TIMESHIFT_DEVICE` (no free elements:
  all 24 are taken, including HEART) → one power wheel slot; pressing it again toggles timepiece/gates; the timepiece
  shows the gate icon. A known limitation, only while the player has the timepiece.

Design decision (2026-10-13, 22:30): the "through the gates" window is OFF by default (`g_viewOn = 0`, enabled with the
`viewon 1` command of the test channel): on floor gates the image is skewed, pieces of the window flicker near surfaces,
dark areas. The gates show their own Void vortex; FPS as without gates (40.7 vs 41.8).
Closing (F) and replacing the gates by recasting: the effect is stopped by its own stop function (burns out over ~0.5 s, sound
`void_portal_end`), a Void flash (`voidgate_pass.def`), the entity is removed after 3 s (`gate_fade_out`, `dying_update`).
Installing the data mods — always all four: `py tools\d2install.py install d2mods\NormalBoltSleeps d2mods\Boost
d2mods\NonLethal d2mods\VoidGate` (VoidGate alone wipes the others — this happened on 13.10 at 22:18, fixed at 22:33).

Window: a polygon of 10 parallelograms (the stock rectangle with a skewed axis), 20 sides, inscribed in the oval at
0.93 — no "staircase". Works on any gate the player is standing in front of (one window per frame).

Entering floor gates: velocity is estimated only from a changed position (the tick runs 3-4 times per position
update: repeats gave zero velocity and a 28 m/s fall slipped through), plus a check of the segment between the two
positions (whether it crossed the plane inside the oval).

## 0. Design (decisions made on 2026-10-13)
- Name: **«Врата Бездны»** (EN: *Void Gate*). Left hand. Originally for Emily; since 2026-10-13, on request, it is
  available to **both** (`m_canBeUsedBy = "USAGE_BY_CORVO USAGE_BY_EMILY"`). The game takes the journal header with the
  character prefix (`emily_` → `corvo_` is substituted in code), so both exist: `emily_/corvo_header_p_voidgate_is`.
- Placement as in Portal: each cast alternately places the entry/exit at the aim point; a new cast replaces the oldest
  portal; the pair lives until replaced.
- See-through portals (second-camera render); passing through preserves velocity and view direction.
- Upgrades: **Far Rift** (placement range), **Through the Void** (NPCs, corpses, items and bolts pass through the
  portals), **Momentum Preservation** (exit velocity is amplified — a "fling" as in Portal).

## 1. What already exists in the game (recon 2026-10-13)

### The cut TIMETRAVEL power — a free slot
The exe contains `ARK_PLAYER_POWER_TIMETRAVEL` (string 0x200d528, between DOPPELGANGER and POSSESSION), but it is not
in `playerpowersystem.cpntplayerpowersystem.decl` (11 powers), nor in the texts, nor in the items. There is no native
power component for it either (no `arkCpntPowerTimeTravel` class exists).
Plan: take this id for the portal — the power wheel/journal code already knows this power number.

Unused upgrade ids (candidates for the portal branch): `ARK_POWERUPGRADE_BLINK_BLINKATTACK`,
`ARK_POWERUPGRADE_FARREACH_PULL_ATTACK`, `ARK_POWERUPGRADE_MESMERIZE_ANIMAL`, `ARK_POWERUPGRADE_MESMERIZE_COMBAT`
(check whether any behavior in the code is tied to them).

### The Outsider's Timepiece (`timeshift`) = a ready-made portal renderer
- Component `cpntTimeShiftDevice` (`models/weapons/timeshift_device/timeshift_device.cpnttimeshiftdevice.decl`):
  swaps `m_materialToReplace` (the lens) for `m_portalMaterial` on the fly =
  `models/effects/materials/gameplay/powers/timetravel/portal.material`.
- Material → shader `rendering/timeTravelPortal` (`generated/decls/renderprog/rendering/timetravelportal.decl`).
  The shader takes the image from the render target **`$(rendering/portal)`** by **screen** coordinates
  (`screenPosToTexcoord(sv_position)` × `$(rendering/portalResolutionScale)`), plus refraction, masked blur,
  "glitch" noise (`tsd/FailFactor`).
  ⇒ the engine renders a **second full view** (another camera) into the RT `_TimeTravelPortal`; the lens shows it in its pixels.
  A real portal needs the same: portal camera = T(B) · T(A)⁻¹ · player camera.
- Strings in the exe: `m_portalViewOrg` (0x1fb5158), `m_portalViewConstantBuffer`, `m_umbraVisibilityInPortal`
  (separate Umbra culling for the portal view), `rendering/portal`, `rendering/portalResolutionScale`,
  `_TimeTravelPortal`, cvar `ark_timeshift_skipportal` ("Skip portal rendering"), fields `m_portalImage`,
  `m_portalActivated`, `m_portalMaterialSet`, `m_portalEntrance`, `m_cachedTimeshift`.
- Teleport: `arkPlayerStateUseTimeShiftDevice` (FSM: STATE_TELEPORT_LOOP / TELEPORT / TELEPORT_FAIL),
  midnight scenes `gadget_timeshift_scan_teleport(_fail)`, effect `tshift_transition_01.fx`,
  post-effect `env/gameplay/powers/timetravel_transition_01`.
- "A Crack in the Slab" — two maps (past/present) loaded at the same time; the transition is an offset.

### The Mesmerize portal — a native look for the edges
`arkCpntMesmerizePortal` / `cpntMesmerizePortal` — the Void rift that Mesmerize's spirit comes out of (Emily's power).
A candidate for the visuals of the portal edges (model/FX/sound).

### How a power is set up in data (using Far Reach as an example)
- Item `models/powers/grapple/grapple.inventoryitem`: `m_type = TYPE_POWER`, keys `m_name/m_description/
  m_strategicTips`, `m_equipment` (entityDef), `m_uiIndex = ARK_HUD_ELEMENT_ITEM_FAR_REACH`, `m_journalImage`,
  `m_journalHeaderImage`, `m_runeCost`, `m_tutorial`.
- Entity `models/powers/grapple/grapple.def`: the arm model + components `cpntPowerGrapple` (behavior: mana 20,
  cooldown, cast midnight scenes, aim/impact FX), `cpntAnimEvolved`, `cpntMidnightProxy`, `cpntAIAttentionSource`.
- Emily's starting powers: `cpntplayeroutsidermark/.../emily/outsidermark.cpntplayeroutsidermark.decl` (`m_startingPowers`).
- Availability: `inventoryitemcollection/.../emily/inventory_collection` (inherits base; the other character's powers have `m_maxQuantity = 0`).
- Upgrade trees: `playerpowersystem.cpntplayerpowersystem.decl` (`m_powers[] {m_id, m_upgrades[] {m_upgradeId, m_subUpgrades}}`).
- HUD slots (enum): `ARK_HUD_ELEMENT_ITEM_TIMESHIFT_DEVICE` exists; there is no dedicated one for the portal.
- Texts: `Power_<x>_name`, `Power_<x>_desc`, `Power_strategy_<x>_desc`, `Power_strategy_<x>_<upgrade>_desc`
  in `strings/english_m.lang`, `russian_m.lang`.
- UI: Iggy (`ui/lib_journal_powers.iggy` — the powers journal, `powerwheel.iggyfile.decl` — the power wheel).
  **The journal is data-driven**: the AS3 has only `PowersGameDataProvider`, `PowerDefinition`, `UpgradeTreeDataProvider`,
  `UpgradeTreeBranchDefinition`, `UpgradeTreeElementCost`, states Available/Bought/Locked/Unavailable, and there is a
  scrollbar — no names of specific powers. ⇒ a new power in data + a correct native provider = it shows up in the journal
  without editing Iggy (verify that the provider does not filter by a fixed list of ids).
- Upgrades: `TYPE_POWER_UPGRADE` items in `components/characters/player/base/attributes/*.inventoryitem`
  (`m_name/m_description/m_strategicTips`, `m_modifiers[] {m_attribute = ARK_ATTRIBUTE_*, m_increase}`,
  `m_journalImage`, `m_runeCost`). The id → item link is in `playerpowersystem` `m_powerUpgradeItemList`
  (`enumItem[ARK_POWERUPGRADE_X] = { m_item = ... }`), i.e. **data**. For `MESMERIZE_COMBAT` there is an entry
  (`mesmerize_incombat`); for `BLINK_BLINKATTACK`/`FARREACH_PULL_ATTACK`/`MESMERIZE_ANIMAL` — needs checking.
- There is no similar "power id → item" table in the decls (power item paths are not hardcoded in the exe either) — the
  link probably goes through the native power component class (virtual id). This is the main question for Ghidra.
- Upgrade effects = `ARK_ATTRIBUTE_*` attributes (enum in the exe). Custom attributes cannot be added without code; our
  native code can read unused attributes of other powers or simply check which upgrades were bought.

### renderView_t (reflection, table at 0x141770130)
| Field | Offset | Type |
|---|---|---|
| fov_x_RADIANS / fov_y_RADIANS | 0x9C / 0xA0 | float |
| cramZNear | 0xA4 | bool |
| vieworg | 0xA8 | idVec3 |
| previousvieworg | 0xB4 | idVec3 |
| **m_portalViewOrg** | 0xC0 | idVec3 |
| viewaxis | 0xCC | idMat3 (0x24) |
| **customClipPlane** | 0xF0 | idPlane (0x10) — oblique clip plane for the portal |
| explicitProjectionMatrix / useExplicitProjectionMatrix | 0x100 / 0x140 | idRenderMatrix (0x40) / bool |
| m_umbraOffsetWs | 0x144 | idVec3 — offset for visibility culling |
| m_worldWind / forceIdentityViewMatrix / parmBlock | 0x150 / 0x15C / 0x160 | |
The offsets are inferred from type sizes (the compiler shuffled the order of immediates in the initializer) —
confirm in Ghidra/on the live process.

### Where the timepiece's portal rendering code lives
- cvar `ark_timeshift_skipportal`: object ~0x143BE0960 (initializer 0x141583850). Read at 0x140511224.
- The RT `_TimeTravelPortal` is created at 0x14050FFF0. The neighboring functions 0x14050AA90…0x140514040 use
  statics of the same module — this is the timepiece's portal render module (decompile it entirely).
- Reflection: `m_portalMaterial` 0x141910A20, `m_portalActivated` 0x1419114A0, `m_portalEntrance` 0x14191B120
  (fields of arkCpntTimeShiftDevice / related classes), `ARK_PLAYER_POWER_TIMETRAVEL` → enum table 0x1424CADA8,
  code 0x141BA3050.

### Timepiece portal rendering (Ghidra, 2026-10-13)
Class `arkCpntTimeShiftDevice`, vftable 0x141E87E18 (slot 3 = init 0x14050FFF0).
- **init 0x14050FFF0**: creates a screen-sized RT (renderSystem `DAT_14359fb20` vt+0x218/+0x220 = width/height,
  format 0x30040, `FUN_1403751e0`) → `this+0x68`; finds the level's timeline manager
  `FUN_1409faac0(*(G+0x312ff8), &DAT_143c06920, 0)` → `this+0xa0`; model bones `tsd_viewport1..6` → array `*(this+0x100)`.
- **update 0x140511210**: "can teleport?" `0x140511FE0` (player position ± timeline offset from
  `*(*(mgr+0x28)+0x230)+0xb0`, free-space check with Havok shape casts, stepping up by 0.2) → effects/material
  (FailFactor via the parm handle `DAT_14254e388`); bails out if `ark_timeshift_skipportal` (`DAT_143be0988`).
- **portal render 0x140511580** (flag `this+0xa8 & 4`, game state == 3):
  1. the player's current view `V = list[G+0x3ea57][last] - 0x20`; the player's renderView = `V+0x30`
     (vieworg `V+0xd8`, viewaxis `V+0xfc`);
  2. `FUN_140510FC0(this, &org)` — org = vieworg ± timeline offset;
  3. the portal view is the object `this+0x150` (contains the renderView at +0; huge, up to ~+0x24d60, Umbra buffers):
     `FUN_140a0f8d0(view, org, axis)` (writes vieworg +0xa8, m_portalViewOrg +0xc0, viewaxis +0xcc, flag +0x39c0),
     `FUN_140a13240(view, V+0x30)` (copies settings), `FUN_140a10780(view, view, hq)` (builds the frame);
  4. render slot: renderer = `G->vt[0x48]()`, slot = `r->vt[0xc0](r, r->vt[0xb0](r)-1)`, `FUN_140064a90(slot, view)`,
     `slot+0x50` = timeline flag; **the player's renderView `+0xc0` (m_portalViewOrg) := org**;
  5. the 6 frame bones are projected → screen rectangle → `renderSystem->vt[0x1b8](rs, RT, slot, &rect, &clear)` —
     renders the view into the RT **only inside the rectangle** (scissor);
  6. the RT is bound to the parm `rendering/portal` (handle `DAT_14254e368`) on the owner's render model
     (`*(*(this+0x10)+0xf0)+0x1950`): `FUN_1403dc040(parms, h)`, `FUN_1403dbcf0(v, h, &(RT+0x98))`.
- update/render are called via the component scheduler (`FUN_140b01a10` / `FUN_140b01a70`).
- Parm declaration table: `{reg 0x142710e10, name, handle...}`, 0x20 bytes each, from 0x14254e350 (`rendering/portal`).

### The power number is a decl field! 
`arkDeclCpntPower` (the base decl of all power components): `m_cooldown` 0x148, `m_manaCost` 0x150, `m_manaReplenish` 0x154,
`m_canBeUsedBy` 0x160, … **`m_powerId` (arkPlayerPower_t) 0x190** (reflection 0x1419A7AA0). Not set in the stock decls
(default value comes from the class) ⇒ in our own component decl we write `m_powerId = "ARK_PLAYER_POWER_TIMETRAVEL";`.
Enum values (0x141BA3050): NONE 0, BENDTIME 1, BLINK 2, SHADOWWALK 3, MESMERIZE 4, DOPPELGANGER 5, GRAPPLE 6,
POSSESSION 7, **TIMETRAVEL 8**, DARK_VISION 9, WINDBLAST 10, DEVOURING_SWARM 11, PHANTASMAL_LINK 12,
DOPPELGANGER_SWAP 13, ALL 14.
Power component classes (vftable): Power 0x141EB9798, Grapple 0x141E86EE8, Blink 0x141EB9E18, Mesmerize 0x141EB9F98,
Doppelganger 0x141E86898, ShadowWalk 0x141EBA358, PhantasmalLink 0x141E870F8 and others (ctor Grapple 0x1404FEF10).

### Power component: methods (vtable arkCpntPower 0x141EB9798 / arkCpntPowerGrapple 0x141E86EE8)
Component fields: `+0x08` decl (`decl+0x190` = m_powerId, `+0x150` m_manaCost), `+0x10` owner entity,
`+0x30` animator, `+0x64` use counter, `+0x70` last use time (double), `+0xa4` "in use".
| Slot | Base | Grapple | Meaning |
|---|---|---|---|
| 4 (+0x20) | 0x140737AA0 | 0x1404FFB00 | attach: **`powerSystem[0x6E + m_powerId] = this`** (registration by number) |
| 32 (+0x100) | 0x140737580 | 0x140500110 | can it be used (cooldown) |
| 35 (+0x118) | 0x140737630 | 0x1405001C0 | start of use (aiming) |
| 36 (+0x120) | 0x140737830 | **0x140500240** | **cast**: with a valid target (`+0xD7`) Grapple takes the point `+0x120` (and `+0x12C`), enters the pull state, then the base cast (mana `FUN_1408882e0`, time, counter) |
| 37 (+0x128) | 0x140737A20 | 0x140500540 | end/cancel |
| 43 (+0x158) | — | 0x140500700 | UI "targetIsValid" |
| 44 (+0x160) | 0x140737780 | 0x140500070 | mana cost |
`FUN_1409faac0(entity, &typeDesc, flags)` = GetComponent; `DAT_143c06920` = the **player component** type
(in the timepiece `this+0xa0` = the player component, timeline offset = `*(*(player+0x28)+0x230)+0xb0`).
Hook plan: hook Grapple's slot 36 — if `decl->m_powerId == 8`, place a portal at the target point instead of pulling,
then call the base cast (`0x140737830`) for mana/cooldown.

### Packages with new resources
Void Installer ≥1.1 supports new resources (`AddNewResource`, `CreateNewEntry` in Void.Utils.dll), but the marker format in
`.voidIndex` has not been decoded (no .NET SDK for ILSpy). Plan: teach `tools/d2mod.py` to install mods by itself
(voidmod indexes + `master.index`, as described above) and to add new index entries (type/name/path).

A tool that does not need Ghidra: `tools/x64refs.py` (`str`, `sref`, `refs`, `dump`, `dis`; functions from `.pdata`).

### Graphics (2026-10-13) — `tools/make_voidgate_art.py`
Drawn in code (PIL) in the style of the stock icons; previews in `d2mods/VoidGate/art/`.
- Journal: `j_pwr_voidgate_is` (148, BC3), upgrades `j_pwr_voidgate_{far,through,momentum}_is` (120), header
  `emily_header_p_voidgate_is` (620×224, based on `emily_header_p_blink`). New resources `generated/image/models/guis/
  journal_powers/*.bimage7`, resource name `models/guis/journal_powers/<x>.png`. Writing `.bimage7`: `tools/bimage_write.py`.
- **Power wheel/HUD**: icons — the `LIB_EquipmentIcon_container` symbol in `ui/lib_wheel.iggy` (one frame per `EnumEquipmentIcons`:
  P_* powers, G_* gadgets; none free). Textures — `ui/lib_wheel.raw.iggytex`, the table = **iggy subfile 8**:
  u16 count (47), u16, u32 total, 8 zeros, 12-byte entries `{u32 offset, u8 fmt (0x0D BC7 | 0x00 RGBA8), u8 1, u16 w,
  u16 h, u16 bitmapId}`. Iggy header: magic 0xED0A6749, u32 subfile count @0x1C, 16-byte entries from 0x20
  `{id, size, size, offset}`. Powers have 2 textures each (dim #16–27, bright #30–41), gadgets one each.
  Void Gate uses `m_uiIndex = ARK_HUD_ELEMENT_ITEM_TIMESHIFT_DEVICE` → texture **#14 (id 57, 92×104 BC7)**
  is redrawn (side effect: the timepiece in mission 7 has the same icon). BC7 — `etcpak.compress_bc7`.

### How the journal finds powers (verified in game 2026-10-13)
- The journal (`arkJournalSectionPowers::PreparePowerUpgradesData`, 0x140620040) takes the **player's inventory entries** with
  `item->m_type == 9` (TYPE_POWER; `m_type` @ decl+0x60), filter `FUN_140621230` (the other character's powers with zero
  quantity are dropped unless `ark_ui_view_all_powers_in_journal` is on), the power id comes from the power system's pairs
  `powerSystem+0x80 {itemDecl, powerCpntDecl}` (`powerCpntDecl+0x190` = m_powerId), the tree from `decl+0x68 m_powers`
  (stride 0x18). No tree → fatal error "Missing upgrade info for power".
- The inventory contains entries for all **loaded** item decls (Corvo has 819). A decl is loaded only if it is referenced
  ⇒ our item is added to `inventory_collection` (base, `m_maxQuantity = 1`) — after that there are 12 pairs, the power is in
  the journal (12th cell, under the mask edge — the grid is laid out for 11), can be bought with runes, upgrades available.
- Power wheel: the power is visible, but in the **gadget arc** (due to `m_uiIndex = …TIMESHIFT_DEVICE` → G_*), selectable;
  the HUD shows our icon. Cast = Far Reach (aim, pull); hint «Отменить Притяжение» ("Cancel Far Reach") is Grapple's text.
- Live debugging: `tools/d2live.py powers` (reads game memory; D2 is not running as admin).
- Automated game control: input must be **held** (`hold_key` ≥0.3 s), the window brought to the front via
  `SetForegroundWindow`; journal — J, power wheel — middle mouse button/CapsLock (hold), power — RMB.

## 2. Mod architecture (draft)
1. **Data** (Void Installer, `d2mods/VoidPortal`, `tools/d2mod.py`): a power item with id TIMETRAVEL, a power
   entity (clone of Far Reach), a branch in `playerpowersystem`, availability for Emily, RU/EN texts, icons.
   ⚠ `d2mod.py build` cannot handle **new** resources yet — extend it or reuse existing decls.
2. **Code** (x64 `dinput8.dll` in the game folder): power behavior (aim → portal A/B), passage (transfer preserving
   velocity), rendering the view from the portal via the timepiece mechanism (`m_portalViewOrg`, RT `_TimeTravelPortal`),
   a portal surface material modeled on `portal.material`.
3. **Stages:** (a) exe research; (b) portal without the "window" (passage); (c) the "window"; (d) journal/power wheel/upgrades; (e) polish.

## 3. Open questions
- Where in the exe `m_portalViewOrg` is set and how portal view rendering is enabled (is the timepiece active on only one map?).
- How powers are mapped to ids (a virtual method of the component? a field?) and what the journal/power wheel do with an unknown id.
- Whether the `cpntPowerGrapple`/`cpntPowerBlink` class can be used as a base without an id conflict.
- Whether Void Installer supports new index entries (the `i32 -1` field in `.voidIndex`).
