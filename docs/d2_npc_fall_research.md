# D2 NPC falling: why a teleported NPC "walks on air", and how to make it fall properly

**Implemented 2026-10-13 (vg.c "NPCs: falling, momentum"), tested once in game:** a live guard sent out of a wall
gate 5 m up got START_RAGDOLL (plain, activeRagdoll NULL) right after Teleport_r and fell to the floor as a ragdoll
(no more walking on air). Velocity: ent+0x33C turned through the gates; VELOCITY_IMPULSE once physSys+0x238 reads
0/2 (or after 400 ms); bodies already ragdolled get the impulse at once. Not yet: the direction-matched active
ragdoll decl, the bark / witness cue, physSys+0x3AC reset for floor-gate loops. Note: >4 m below the ragdoll start
is an instant kill for NPCs (the stock rule) -- a gate exit high up kills, like Windblast off a roof.


Research 2026-10-13 (Dishonored 2 GOG 1.77.9.0, no ASLR, static Ghidra + read-only live memory reads of the running
game). Tags: **[V]** verified in code (and/or live), **[L]** verified live, **[I]** inferred / likely, **[?]** guess.

## TL;DR

* A live NPC has **no falling locomotion**. `arkLocomotionModeType_t` = IDLE / FRONTAL_MOVE / STRAFE_MOVE / TRAVERSAL;
  fall animations exist only inside navmesh traversals (VAULT_FALL, DROP_DOWN: `m_fallLoop`, `m_landing`). The
  game's only "uncontrolled fall" reaction for a live NPC is **ragdoll**: `arkCpntAIPhysicsReaction` turns a free fall
  into `ARK_MSG_START_RAGDOLL` with the `m_fallRagdollSettings` active ragdolls (`ar_throwhit_*`), a delayed
  `AI_BARK_TRIGGER_UNCONTROLLED_FALL` bark and an `ARK_CUE_WITNESS_THROWHIT` cue. **[V]**
* That reaction is gated by the navigation **"prevent falling"** request, which is ON for every NPC that walks
  normally or idles in a midnight scene (nav request `m_preventFalling = (character->m_action == NULL)`), and by "body
  not keyframed". A Teleport_r into the air therefore never ragdolls: the character controller still integrates
  gravity (unless the body is keyframed with gravity off -> it hovers), the walk anim keeps playing, it lands softly.
  **[V][L]**
* NPC fall damage exists **only while ragdolled** (`arkCpntPhysicsSystemEvolved` contact callback, mode 0/2): a
  drop of more than **4 m** below the z where the ragdoll started = `DAMAGETYPE_PHYSICS | INSTANTKILL`; otherwise an
  impact faster than **13 m/s** deals `(v-13)/(20-13) * 40` damage. **[V]**
* **Recommended fix:** right after Teleport_r, when the exit is airborne, post the same two messages the engine uses
  for throw hits / fall ragdolls: `ARK_MSG_START_RAGDOLL` (0xB9) then `ARK_MSG_VELOCITY_IMPULSE` (0x149, non-additive)
  with the velocity rotated through the gates. Optionally arm the bark. Corpses / already-ragdolled bodies only need
  the VELOCITY_IMPULSE (Teleport_r zeroes their body velocities).

---

## 1. How an NPC detects that it is falling

### 1.1 Object graph (NPC entity -> components) **[V]**

| What | How to get it | Notes |
|---|---|---|
| `arkCpntNavigation` | GetComponent(ent, **0x143C32680**, 2) | vtable 0x141EB7528 |
| `arkNavmeshCharacter` | nav **+0xA8** (`m_character`) | size 0x190 |
| `arkCharacterRayCast` (character rigid body) | navChar **+0x98** (`m_rigidBody`) | vtable 0x141E4AF58 |
| `arkCpntAIPhysicsReaction` | GetComponent(ent, **0x143C31FC0**, 2) | vtable 0x141EAD3E8; decl at +0x08 |
| `arkCpntPhysicsSystemEvolved` (ragdoll) | GetComponent(ent, **0x143C0B1A0**, 2); also nav +0x140, AIPR +0x90 | vtable 0x141ED5B38; decl at +0x08 |
| `arkCpntAnimEvolved` | GetComponent(ent, **0x143C36D60**, ...); also nav +0x40, AIPR +0x88 | |
| `arkCpntReferenceFrame` | nav +0x48; AIPR +0xB8 (desc 0x143C3C400) | writes entity transform + velocity |

(Type descriptors resolved from their static initialisers: nav 0x141613E50, AIPR 0x141613610, anim 0x14161B7B0,
physics system 0x1415D3BC0.)

### 1.2 Ground probe: `arkCharacterRayCast` **[V]**

* `FUN_1401D10D0` = per-step pre-update of the character body. Calls the ground probe `FUN_1401D1B30`, then:
  * if **prevent falling** (`+0x130` bit1) and supported -> `FUN_1401D1F50` (navmesh boundary planes keep the NPC
    from walking off edges; `FUN_14020B730` feeds those planes into `+0x110`);
  * if the hknp body is **keyframed** (body flags bit2), **gravity** (`+0x130` bit0) is on and not supported ->
    `vel += g(world+0x970) * gravityFactor(+0x12C) * dt` and the velocity is pushed with a 0x90200 command.
* `FUN_1401D1B30` (probe): ray from 0.1 m above the feet down (extended by step-down when supported). Hit ->
  `+0xB1` support = 2 SUPPORTED if the normal is flat enough (`normal.z > +0x50`) else 1 SLIDING, `+0xB0` = moving
  ground, `+0xC0` normal, `+0xD0` ground velocity, clears `+0x130` bit2. Miss -> first miss only sets bit2 (grace
  frame), the **second consecutive miss sets `+0xB1 = 0` UNSUPPORTED**.
* `arkCharacterRayCast` fields: +0x18 hknp body id, +0x28 hknpWorld*, +0xB1 support (0/1/2), +0x12C gravity factor,
  +0x130 flags: **bit0 gravity, bit1 preventFalling, bit2 probe-missed-once** (ctor sets 7).

### 1.3 Navmesh character -> navigation message **[V]**

* `FUN_14020B9F0` (post step, called from nav `FUN_14070D8D0`): copies the body state into the navmesh character:
  `m_ground` (+0x10, `arkPhysicsGround {m_dynamic, m_state +0x14, m_normal, m_velocity, m_angular}`) and
  **`m_physics` (+0x0C) = support state** (`arkNavmeshCharacter::physics_t`: 0 PHYSICS_UNSUPPORTED, 1 SLIDING,
  2 SUPPORTED). On change it calls every listener's slot 6; the navigation's listener (`0x140715950`) posts
  **`ARK_MSG_NAVIGATION_PHYSICS_CHANGED` (4)** with the new value.
* Nav pre-update `FUN_14070D1D0` (every frame, before the step):
  * keyframing requests (`m_keyframingReqMgr` nav+0xAB8, entries 0x1C `{int reason; uint flags(bit0 = gravity);
    int handle; ...}`): requests present -> body made keyframed (`FUN_14020AD30`) and gravity bit = top request's
    gravity; none -> a keyframed body is made dynamic again (`FUN_14020AE30`) and the gravity bit set to 1 (a body
    that is already dynamic keeps its bit; live: 1). Change of keyframed state posts
    **`ARK_MSG_NAVIGATION_KEYFRAMING_CHANGED` (5)**.
  * prevent-falling requests (`m_preventFallingReqMgr` nav+0xC00, array at +0xC08, count +0xC10, entries 0x18
    `{bool value; int handle@4; int priority@8; float duration@0xC; int type@0x10}`): the NAVIGATION request
    (handle nav+0xD44, priority 0, added at nav start) is updated every frame to
    **`value = (navChar->m_action(+0xD8) == NULL)`** (no navmesh traversal action => prevent falling); while a midnight
    plays (anim +0x2A6C == 1 && +0x2A68 > 0) a MIDNIGHT request (handle +0xD48, priority 1) takes the midnight's
    `m_preventFalling`. Top request value -> `rayCast+0x130` bit1. Priorities: 0 NAVIGATION, 1 MIDNIGHT, 2 SYNC_ANIM.

### 1.4 The fall reaction: `arkCpntAIPhysicsReaction` **[V]**

* Start (`FUN_14069B0A0`, vt slot 5): if decl+0x338 (always 1, set by the decl ctor 0x14069A030) subscribes msgs 4
  and 5 to **`FUN_14069FFE0`**:
  ```c
  if (navChar->m_physics == PHYSICS_UNSUPPORTED && !bodyIsKeyframed)
      this->m_fallRagdollCheckStart(+0x140) = now + decl->m_checkDelay(+0x300);   // schedule think
  else this->m_fallRagdollCheckStart = -1.0;
  ```
* Update `FUN_14069B7A0` (runs while the component is scheduled; `FUN_14069C320` decides when it may sleep):
  ```c
  if (+0x140 >= 0 && nav started && now > +0x140) {
      f = rayCast+0x130;
      if (!(f & 2) /*no preventFalling*/ && (f & 1) /*gravity*/ && !inUncrossedTraversal(anim+0x2BAC != 6 && !(anim+0x2BCC & 0x10))
          && -ent->vel.z(+0x344) > decl->m_minimumFallSpeed(+0x304)) {
          dir = FUN_14080E2E0(ent+0x30C, -vel);                 // A4DIR 1..4
          post ARK_MSG_START_RAGDOLL {decl->m_activeRagdoll[dir] (= FUN_1406A0E60(decl+0x300, dir)), type 0};
          +0x140 = -1;  +0x148 (m_uncontrolledFallBarkStart) = now + decl->m_barkDelay(+0x308);
          post ARK_MSG_AI_REQUEST_CUE(0x73) to the player {22 = ARK_CUE_WITNESS_THROWHIT, npc origin};
      }
  }
  if (+0x148 >= 0 && now > +0x148) {
      if ((physSys+0x238 & ~2) == 0 /*ragdolled*/ && !(ent+0xD1 & 0x28) /*alive & conscious*/)
          post ARK_MSG_AI_BARK_TRIGGER(0x4F) {"AI_BARK_RULE_HANDLER_COMBAT", 53 = AI_BARK_TRIGGER_UNCONTROLLED_FALL};
      +0x148 = -1;
  }
  ```
* Defaults (decl ctor, not overridden by `aiphysicsreaction_npc.decl` which only sets `m_activeRagdoll`):
  `m_checkDelay` 0.1 s, `m_minimumFallSpeed` 1.0 m/s, `m_barkDelay` 0.1 s. `fallRagdollSettings` = decl+0x300:
  +0x300 checkDelay, +0x304 minimumFallSpeed, +0x308 barkDelay, **+0x310 `m_activeRagdoll[5]`** indexed by
  A4DIR (0 NONE, 1 FRONT, 2 BACK, 3 LEFT, 4 RIGHT) -> FRONT = decl+0x318 ... RIGHT = decl+0x330.
* AIPR runtime fields: +0x88 anim, +0x90 physSys, +0xA8/+0xB0 nav handle/ptr, +0xB8 refFrame, +0xE4/+0xF0
  throw-hit linear/angular impulse, +0x140 `m_fallRagdollCheckStart` (double, -1 = off), +0x148
  `m_uncontrolledFallBarkStart` (double), +0x150..+0x152 bit flags (`m_isInThrowHit`, `m_throwHitPendingImpulse`, ...).
* Time base used by these fields: `now = (double)(float)(*(double*)(game+0x2E7440 + slot*0x50) - *(double*)(ent+0x2A0))`,
  `slot = ((int*)0x14259C818)[*(int*)(ent+0x2AC)]`; frame dt = `*(float*)(game+0x2E7450 + slot*0x50)`.

## 2. Why a Teleport_r into the air skips all of this

`idEntity::Teleport_r` 0x1409FD300: yaw-only axis if the entity has a navigation component, posts
`ARK_MSG_TELEPORTED` (0xC3), calls every started component's vt slot 22 (+0xB0) `Teleport(pos, axis, resetVel)`, writes
origin/axis, zeroes ent+0x33C..+0x350 only if resetVelocity. **[V]**

* Navigation teleport `0x140710990`: BeginManualControl(zero vel, gravity off) `FUN_14070F100` -> set the character
  body transform (`FUN_140E73DD0` -> hknp setBodyTransform; body velocity untouched) -> EndManualControl
  `FUN_14070F380` (restores the gravity bit). The probe/support state is NOT reset: it goes UNSUPPORTED two steps
  later, msg 4 fires. **[V]**
* Physics system teleport `0x1408B3D60`: moves every ragdoll body and **sets their velocities to zero**
  (`FUN_1401F0CD0(..., &zero, &zero)`). Irrelevant for an animated NPC, but it kills a corpse's momentum. **[V]**
* AIPR does not react to TELEPORTED. When msg 4 (UNSUPPORTED) arrives:
  1. If the body is keyframed (NPC in an idle/scripted midnight with a keyframing request) the check is not armed;
     with that request's gravity flag = 0 (`flags` 0x14 seen live) the body does not even fall -> "walks on air".
  2. If armed, the update still fails on **`rayCast+0x130` bit1 = prevent falling**, which is 1 for all active NPCs
     (no navmesh traversal action) -> no ragdoll, no bark.
  3. The physics system stays animated (mode 3), so the contact callback skips fall damage.
  Result: the dynamic character body falls under gravity while the locomotion graph keeps its walk/run blend; on
  landing m_physics returns to SUPPORTED, AIPR disarms (+0x140 = -1). **[V]**
* Live (read-only, 51 nav entities in the current level, 2026-10-13) **[L]**: every animated guard (physSys mode 3)
  has prevent-falling top request value 1 (NAVIGATION, plus MIDNIGHT when idling), `rayCast+0x130` = 0x72/0x73,
  support 2, `m_fallRagdollCheckStart` = -1. Idling guards have 1 keyframing request (handle not the nav's own),
  flags 3 (gravity on) or 0x14 (gravity off). Bodies on the floor (entity +0xD1 = 0xA0) are in mode 0 with no requests.

## 3. What the mod can do

### 3.1 Posting entity messages (verified encoding) **[V]**

```c
// queue: game+0x1F5250 (idLinearAllocator-like, lock-free bump + spinlock, used from job threads by the engine)
// header: (payloadSize << 16) | msgId, then the receiving entity handle (ent+0xA8), then the payload.
static void *PostEntityMsg(uint32_t msgId, uint32_t handle, const void *payload, uint32_t size) {
    uint8_t *g = *(uint8_t **)0x142710D68;
    _InterlockedIncrement((volatile long *)(g + 0x1F5278));                 // stats counter, the engine does it
    uint32_t *m = ((uint32_t *(*)(void *, int))0x14013E790)(g + 0x1F5250, (int)size + 8);
    m[0] = (size << 16) | msgId;  m[1] = handle;  memcpy(m + 2, payload, size);
    return m;
}
```
Seen in the binary: 0x1000B9/alloc 0x18 (START_RAGDOLL), 0x1C0149/alloc 0x24 (VELOCITY_IMPULSE), 0x40004/alloc 0xC,
0x10005/alloc 9, 0xC3/alloc 8. Ready-made poster for VELOCITY_IMPULSE: `FUN_1404B8D30(unused, const uint32_t payload[7],
idEntity *ent)`.

Message ids (ARK_MSG enum, 467 values, table in .data around 0x1424C60C8): 4 NAVIGATION_PHYSICS_CHANGED,
5 NAVIGATION_KEYFRAMING_CHANGED, 0x21 PHYSICS_ACTIVATED, 0x4F AI_BARK_TRIGGER, 0x73 AI_REQUEST_CUE, 0x92 AI_LAUNCH,
0xB7 ENABLE_EARLY_RAGDOLL, 0xB8 REMOVE_UP_VELOCITY_ON_RAGDOLL, **0xB9 START_RAGDOLL**, 0xBA STOP_RAGDOLL,
0xBB STARTED_RAGDOLL, 0xC2 TELEPORT, 0xC3 TELEPORTED, 0xD4 WIND, 0xFA IMPULSE, 0x130 NPC_THROW_HIT,
**0x148 CLEAR_VELOCITY, 0x149 VELOCITY_IMPULSE**.

Payloads:
* `arkMsgStartRagdoll` (0x10): `{ const arkDeclActiveRagdoll *activeRagdoll; int type; int pad; }`, type 0
  START_RAGDOLL (immediate: `FUN_140871980`), 1 ENABLE_EARLY_RAGDOLL, 2 SIMULATE_EARLY_RAGDOLL. Handler
  `0x140875930` stores the decl at physSys+0x2C0 (NULL = plain full ragdoll, non-NULL = powered active ragdoll that
  flails, then ends by duration/impact/low speed). **[V]**
* `arkMsgVelocityImpulse` (0x1C, reflection-verified): `{ idVec3 m_linearVelocity; idVec3 m_angularVelocity;
  bool m_isPlayerInstigator @0x18; bool m_additive @0x19; }`. Handler `0x140862E70` stores it in physSys
  +0x98 lin / +0xA4 ang / +0xB0 instigator / +0xB1 additive / +0xB2 pending; applied in the physics-system update
  (`FUN_14086F9A0` -> `FUN_140873B90`) to every non-fixed body: non-additive = `linear + (angular term)`, additive =
  current + delta. `m_isPlayerInstigator` -> `FUN_1409CB740(...,1,0)` (blames the player for what follows) **[V]**;
  with angular = 0 every ragdoll body gets exactly `linear` **[I]**.

### 3.2 Where the velocity lives / how to rotate it

* **Entity velocity (read this):** ent **+0x33C** linear (world, m/s), **+0x348** angular. The entity transform block
  is `{origin +0x300, axis +0x30C, scale +0x330, linearVelocity +0x33C, angularVelocity +0x348}` (reflection
  0x1417BEA30). For NPCs it is written by `arkCpntReferenceFrame` (`FUN_140894F20`) from the physics state; the
  engine's own fall check and the ragdoll contact damage read it. **[V]** (not observed while an NPC moves **[I]**)
* **Character body (alive, not ragdolled):** body id = `*(uint*)(rayCast+0x18)`, hknpWorld = `*(rayCast+0x28)`;
  motion = `world+0x120 + body[id](world+0x28 + id*0xB0)+0x40 (int) * 0x80`, motion +0x40 = linear velocity (vec4),
  +0x50 angular. Write: `FUN_1401F0CD0(game+0x60280, &bodyId, &linVec4, &angVec4, deferred)` with the engine's
  `deferred = game[0x337069] || game[0x33706A] || (*(game+0x1E8960))[0xC34]` (physics world locked). Teleport_r does
  not touch this velocity (it stays in the OLD world direction). **[V]**
* **Ragdoll bodies:** use ARK_MSG_VELOCITY_IMPULSE (non-additive) — the engine's path.

### 3.3 Recommended sequence (live NPC, airborne exit) — what the engine itself does for a fall / throw hit

```c
vec3 vIn = ent->vel(+0x33C);  vec3 vOut = R * vIn;                 // read BEFORE Teleport_r
Teleport_r(ent, &pos, &axis, 0, 0);  ent[0xD4] |= 4;               // as now
// decide "airborne": exit gate is a wall/ceiling gate with floor clearance > ~1 m, or poll
// navChar->m_physics (nav+0xA8 -> +0x0C) == 0 for >= 0.1 s after the teleport (engine-like).
aipr = GetComponent(ent, 0x143C31FC0, 2); phys = GetComponent(ent, 0x143C0B1A0, 2);
if (phys && (phys->mode(+0x238) & ~2) != 0 /*animated*/ && !(phys[0x241] & 8) /*ragdoll allowed*/) {
    void *ar = NULL;
    if (aipr) { vec3 nv = -vOut; int dir = FUN_14080E2E0(ent + 0x30C, &nv);   // 1..4
                ar = FUN_1406A0E60(*(void**)(aipr + 8) + 0x300, dir); }        // decl->m_activeRagdoll[dir]
    struct { void *ar; int type, pad; } sr = { ar, 0, 0 };
    PostEntityMsg(0xB9, ent->handle, &sr, 0x10);
    // optional, as the engine: bark + witness cue
    //   *(double*)(aipr+0x148) = now + *(float*)(decl+0x308);  then wake AIPR: FUN_14094BCE0(aipr, *(void**)(game+0x1E8960))
    //   PostEntityMsg(0x73, player->handle, {int 22; vec3 npcOrigin}, 0x10);
}
// next mod tick, once phys->mode is 0 or 2 (ragdoll running) — the AIPR throw hit also sends it one update later:
struct { vec3 lin, ang; uint8_t playerInstigator, additive, pad[2]; } vi = { vOut, {0,0,0}, 0, 0 };
PostEntityMsg(0x149, ent->handle, &vi, 0x1C);
```
* **Corpses / unconscious / already ragdolling** (phys mode 0 or 2): only the VELOCITY_IMPULSE (rotated linear, and
  rotated `ent+0x348` angular if wanted) right after Teleport_r — the physics-system teleport just zeroed the bodies.
* **Fall height for re-teleported ragdolls:** physSys **+0x3AC** is the reference z for height damage (set to ent z
  when the ragdoll starts and whenever a contact happens at < 0.1 m/s). For a body cycling through floor/ceiling gates
  set `+0x3AC = exit z` after each teleport, otherwise the drop is measured from where the ragdoll started (can kill
  or never kill depending on the loop). Same idea as the player's "zero the falling state's height".
* Risk: **low-moderate.** The two messages are exactly what the engine posts for fall ragdolls (AIPR), throw hits,
  deaths (`FUN_14069D300`) and the witch "fall on appear" (`arkAITeleportSkill` reappear, `FUN_1404B85F0`: sets
  physSys+0x23C = 2, calls physSys vt[0x1C0/8], posts an additive VELOCITY_IMPULSE of `m_fallOnAppearImpulse` rotated
  by the entity axis). Ragdoll recovery (getting up) / knock-out is then the stock AI path. NPCs with ragdoll disabled
  (physSys+0x241 bit3, e.g. `physicssystem_npc_noragdoll`) ignore START_RAGDOLL.

### 3.4 Alternatives (not recommended)

* Let AIPR do it: add a prevent-falling request with priority 2 (SYNC_ANIM) and value 0 via `FUN_1407127F0(nav+0xC00,
  &outHandle, &info, &value)` (info layout only partly known: `{int ?; int priority; int ?; byte flags}`; removal =
  `FUN_140712920(nav+0xC08, index)` + `--*(int*)(nav+0xD0C)` if type != 0). Still fails for keyframed NPCs (not armed,
  gravity possibly off) and needs request bookkeeping. **[I]**
* Clearing `rayCast+0x130` bit1 from a tick: rewritten every frame by the nav pre-update; order vs AIPR unknown. **[?]**
* Manual control `FUN_14070F100(nav, &vel, angVel, gravity)` / `FUN_14070F380(nav)` drives the character body with a
  fixed velocity request (reference-frame +0xE8 / +0x258 request managers); used by teleport and reference-frame
  attach code. Moves the NPC but gives no fall reaction or damage. **[V]**

## 4. NPC fall damage

`FUN_140873F10` = `arkCpntPhysicsSystemEvolved` contact callback (vt slot 19). **[V]**

```c
if ((this->mode(+0x238) & ~2) != 0) skip;                         // only ragdoll (0) / active ragdoll (2)
relSpeed2 = |ent->vel - other->vel|^2;                            // ent+0x33C
if (relSpeed2 < 0.01) this->refZ(+0x3AC) = ent->z;                 // (done before the mode check)
if (only bodies whose [[body userdata+0x90]+0x58] is 2 or 3)       // [?] probably torso/root parts
if (now - lastFallDamage(+0x3C0) > d->m_noDamageDuration(+0x104)) {
    if (d->m_heightDamage(+0xD0) && refZ - ent->z > d->m_minHeight(+0x100)) dmg = heightDamage, full amount;
    else if (d->m_speedDamage(+0xC8) && relSpeed > d->m_minSpeed(+0xF8))
        amount = clamp((relSpeed - minSpeed) / (m_maxSpeed(+0xFC) - minSpeed), 0, 1) * speedDamage->amount(+0x5C);
    // (with a heightDamage decl, refZ = ent->z on every such world contact, damage or not)
    apply via FUN_140874900(contact, ent, instigator(+0x88 handle), dmg, amount);  +0x3C0 = now;
}
```
* decl `fallDamageProperties` at physSys decl **+0xC8**: speedDamage +0xC8, heightDamage +0xD0, ...,
  minSpeed +0xF8, maxSpeed +0xFC, minHeight +0x100, noDamageDuration +0x104. Ctor defaults: minSpeed 6, maxSpeed 15,
  **minHeight 4 m**, noDamage 0.2 s. `physicssystem_npc` sets minSpeed 13, maxSpeed 20, noDamage 0.3; inherited from
  `physicssystem_npc_limb`: heightDamage = `ragdoll_hit_world_height_damage` (`DAMAGETYPE_PHYSICS DAMAGETYPE_INSTANTKILL`),
  speedDamage = `ragdoll_hit_world_speed_damage` (`m_maxDamage 40`, PHYSICS). No m_minHeight override found.
* refZ is set to ent z when the physics system switches from animated to ragdoll (`FUN_1408725F0`) and at every
  world contact after the 0.3 s window (so a bouncing ragdoll measures from its last contact), so with the
  recommended fix: **exit gate > 4 m above the landing point = death** (like a Windblasted guard off a roof);
  lower exits hurt only if the impact speed is > 13 m/s (gate momentum), max 40 per hit, at most every 0.3 s.
* An animated (non-ragdoll) NPC never takes fall damage -> the current "walk on air and land" is harmless.
* Instigator: physSys+0x88 handle (credited kill); `m_isPlayerInstigator = 1` in the VELOCITY_IMPULSE makes the
  player responsible (`FUN_1409CB740`) — design choice for chaos/kill stats. **[I]**

## 5. Other addresses

| Address | What |
|---|---|
| 0x14069A4A0 / 0x14069A030 | AIPR ctor / decl ctor (defaults) |
| 0x14069B0A0 / 0x14069B3E0 | AIPR start / stop (message subscriptions) |
| 0x14069B7A0 | AIPR update (fall ragdoll check, bark, throw-hit impulse) |
| 0x14069FFE0 | AIPR on NAVIGATION_PHYSICS/KEYFRAMING_CHANGED (arms the check) |
| 0x14070A470 | nav start (adds the NAVIGATION prevent-falling request = true) |
| 0x14070D1D0 / 0x14070D8D0 | nav pre-physics update / post-physics sync |
| 0x140715950 | nav listener: post msg 4 with m_physics |
| 0x14020B9F0 | navmesh character post-step (m_ground, m_physics, listeners) |
| 0x1401D10D0 / 0x1401D1B30 | character body pre-step (gravity) / ground probe |
| 0x140710990 / 0x1408B3D60 | Teleport handlers of nav / physics system |
| 0x14070F100 / 0x14070F380 | nav Begin/EndManualControl |
| 0x14086E470 | physics system start (subscribes 0xB7-0xBA, 0x148, 0x149, 0xFA) |
| 0x140875930 / 0x140862E70 / 0x140875870 | START_RAGDOLL / VELOCITY_IMPULSE / IMPULSE handlers |
| 0x140871980 / 0x140872390 / 0x1408725F0 | start ragdoll / set mode / mode transition (sets refZ) |
| 0x14086F9A0 / 0x140873B90 | physics system update / apply pending velocity |
| 0x140873F10 | ragdoll contact callback (fall damage) |
| 0x1401F0CD0 | set hknp body velocity (world wrapper game+0x60280) |
| 0x14080E2E0 / 0x1406A0E60 | direction (A4DIR) from axis + vector / pick active ragdoll by A4DIR |
| 0x1404B85F0 | witch teleport "fall on appear" (ragdoll + velocity impulse precedent) |

Physics system modes (+0x238 current, +0x23C pending, -1 none) **[V/I]**: 0 = full ragdoll (bodies RBS_DYNAMIC),
2 = powered/active ragdoll, 3 = animated (bodies RBS_KEYFRAMED, start mode for BEHAVIOR_KEYFRAMED decls), 1 = ?.
Entity +0xD1: 0x80 alive guard, bit 0x20 / 0x08 = dead / unconscious (AIPR's "incapacitated" test). **[L/I]**

## 6. Open questions

* Exact moment START_RAGDOLL switches the mode (same physics-system update vs next frame) — hence the advice to send
  VELOCITY_IMPULSE once `+0x238` is 0 or 2. Test in game.
* Whether a ragdolled live NPC that lands off the navmesh is knocked out (`m_putAsleepIfOutOfNavmesh`,
  `m_outOfNavDamage = sleep_no_reaction_damage`) — likely, worth knowing for roof exits.
* The `[[body userdata+0x90]+0x58] in {2,3}` filter in the damage callback and the `+0x3A8` / ent+0x368 bit1 skip of
  speed damage are not decoded.
* Whether AIPR's bark fires if we only write +0x148 (the component must be awake: wake it with `FUN_14094BCE0`).
* entity +0x33C during a navmesh-character fall was not sampled live (all NPCs were idle during the scan).

## In-game results (2026-10-13 evening) -- supersedes the "> 4 m = instant kill" reading above

* Entity +0xD1 flags (checked against the save's corpses): **0x08 = dead** (`corpse_02`, `human_dead`),
  **0x20 = unconscious** (the user's choked guards). The "[L/I]" note above had them swapped.
* A guard sent out of a gate in open air (atrium x 0.7, y 28.5) fell 7.6 m and 11 m: alive both times, with or
  without the module's soft landing. The height kill did not happen in practice.
* NPCs that landed off the navmesh (a dog half inside a wall, a guard on a beam at 26.9 m) were knocked out
  ~1-1.5 s later: AIPR's `m_putAsleepIfOutOfNavmesh` (+0x151 bit 3, set by its event-4 handler 0x14069B740; the
  update 0x14069B837 clears it and posts START_RAGDOLL) -- the stock non-lethal rule.
* Module "soft landing" (vg.c `npc_soft_landing`, test channel `npcsoft 0|1`, on by default): for an NPC or body
  out of a gate, physSys+0x3C0 (lastFallDamage, double) = 1e300 (the contact callback's `now - last > 0.3 s` never
  passes: no height and no impact damage) and physSys+0x3AC (refZ) = its z every frame, until it has been still for
  1 s (6 s at least, 20 s at most); then +0x3C0 is restored.
* Wolfhounds have no working ragdoll on START_RAGDOLL here: they hang in the air for a while.
