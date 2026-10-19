# Dishonored 2: cut / unused content (datamine, 2026-10-16)

Tool: `tools/d2datamine.py` (`cache` reads all 35k stock decls into scratch/decls.pickle, `enums` = engine enum values
no decl uses, `grep <regex>`, `orphans <type>` -- noisy: maps place things through binary files). Exe strings:
`tools/x64refs.py str|sref`. Lang: `strings/<language>_m.lang` (decls name texts by numeric id, so "key unused by
decls" means nothing by itself).

## Powers and upgrades
* `ARK_PLAYER_POWER_TIMETRAVEL` (id 8): a power with no data, no component class. Void Gate uses it.
* `ARK_POWERUPGRADE_MESMERIZE_ANIMAL`: no decl uses it, but the player attribute `ARK_ATTRIBUTE_MESMERIZE_ANIMAL`
  is in the stock attribute list (`attributes_player.cpntattributesplayer`), the exe also has
  `ARK_ATTRIBUTE_MESMERIZE_ANIMAL_DURATION_GAIN`, and the text survives: `Power_strategy_mesmerize_animalmesmerize_desc`
  "Wolfhounds and gravehounds are also affected by the mesmerizing effects of the summoned Void entity."
  -> a late cut. Restorable if Mesmerize's code still reads the attribute (test: give the attribute, mesmerize a dog).
* `ARK_POWERUPGRADE_BLINK_BLINKATTACK`, `ARK_POWERUPGRADE_FARREACH_PULL_ATTACK`: unused ids; the features exist under
  other upgrades (Blink Assault: `slash.isBlinkAttack`, `m_blinkAttackDamage`; pull attack: attribute
  `ARK_ATTRIBUTE_GRAPPLE_PULL_ATTACK`, text `Power_strategy_farreach_pullattack_desc`).
* Unused attributes: `DEFENSE_MODE_CHANCE_SCALE`, `PLAYER_RANGED_ACCURACY_SCALE`.

## Player states (`PLAYER_STATE_*`, 17 of 102 unused by decls)
* `SHADOW_HIDE` (`arkPlayerStateShadowHide` + decl class; a rule `front_choke_shadow_hide` in shadow_walk_choke).
* `WALLCROUCH`, `INBENDTIME`, `NPC_FATALITY`, `RECOIL`.
* `STONE_SHROUD`: a player state for Delilah's Stone Shroud (her skill `arkCpntAIStoneShroud`); in the player modes.
* Multiplayer-like leftovers: `VDM_SELECT_LOADOUT`, `VDM_FORCE_SELECT_LOADOUT`, `VDM_GAME_MODE_INFO`, `RESPAWNING`,
  `WAITING_TO_RESPAWN_FROM_DEATH`, `SPECTATE_AFTER_DEATH`, `TELEPORT_SPAWNING`, `CHARACTER_CHOICE`; assets folder
  `models/environment/vs_map/old_from_disho/` (Dishonored 1 props).

## Developer tools in the data
* 37 `devgui_menu` decls (debug/devgui_*.decl): AI debug, AI cheats, chaos, difficulty, equipment, player, powers,
  upgrades, bone charms, gameflow, tutorials. Most of their commands are stripped from retail (`ark_dbg_*`,
  `ark_playerForceEquip`, `arkBoneCharmGetAll`, `ark_ai_*` ...); left: `god npc`, `buddha npc`, `debugEntityByName`,
  `ark_god_player` / `ark_buddha_player` (read by nothing), `ark_playerSmoothedLoco`. Useful as the full map of the
  internal names (every upgrade item path).
* Debug cameras: `entitydef/debug/{freecam,playerdebugcam,sentinel_cam,static_cam}.def` + `cpntplayerdebugcamera`,
  `cpntplayerdebugcamcontrol` decls -> a photo mode candidate (spawn through the module).
* Test leftovers: `tests/test_gameflow_vaultkey`, `tests/testcoins`, `note_test`, `models/testassets/...`,
  `power_bendtime_cast_*_test` scenes, `ply_leftarm_power_shadowgrapple_no_*` anims.

## Texts of items maybe cut (to check against the shops / blueprint lists)
`Gadget_grenade_muffled_desc` "When triggered, the noise on the explosion is greatly reduced." (muffled grenade).

## Power wheel (ui/lib_wheel.iggy)
* Frames (`EnumEquipmentIcons`): 11 powers `P_BEND_TIME P_BLINK P_DARK_VISION P_DOMINO P_DOPPLEGAENGER P_FAR_REACH
  P_MESMERIZE P_POSSESSION P_RATS_SWARM P_SHADOW_WALK P_WIND_BLAST`, 13 items `G_*` (incl. G_HEART,
  G_TIMESHIFT_DEVICE), EMPTY. No spare frame: a new wheel slot = new frame + shapes + ActionScript enum in the Iggy
  file AND a new HUD element value the exe does not have (26 fixed). Not done.
* Textures (subfile 8 table of lib_wheel.raw.iggytex, 47): powers dim #16-27, bright #30-41 = 12 pairs for 11 frames.
  The spare pair #27 / #41 (bitmaps 83 / 113): Corvo's mask with a ghost double = Corvo's Doppelganger icon (New
  Game+ item `doppelganger_corvo`, double `decoy_entity_corvo`), never shown: one P_DOPPLEGAENGER frame, Emily's icon.
  Sheet: scratch/wheel_textures.png.
