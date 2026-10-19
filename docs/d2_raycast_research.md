# D2 raycasts for gate placement: research notes

**Implemented 2026-10-13 (vg.c "solid-surface check"), OFF by default (`surfcheck 1` in the test channel):**
castRay works from the grapple update and from the player physics frame (no crash in ~40 casts x 25 rays). A 360
degree aim sweep in the Addermire atrium passed only once: an oval centred on the aimed point mostly runs into the
floor / ceiling / pillars ("hangs over an edge", "something crosses the oval"), the aim normal often differs from
the ray normal on ornate walls ("uneven at the centre"). Needed before enabling: Portal-style "bumping" (shift the
gate in its plane until the oval fits, try a few offsets), and probably a looser flatness rule. In-plane rays are
cast 15 cm out of the surface (rubble on floors). Test channel: `surfaceat x y z nx ny nz` -> verdict.


Dishonored 2 GOG 1.77.9.0, `Dishonored2.exe`, base 0x140000000, no ASLR. Research date: 2026-10-13.

Legend:
* **[V]** = seen in code, or checked read-only in the running game (live).
* **[I]** = inferred from code but not fully traced.
* **[?]** = a guess, or general Havok knowledge that was not checked in this binary.

Goal: refuse gate placements that are not on a solid, flat surface. Bad surfaces are railings, grates, fences, foliage, glass, and ledges that are too small for the oval.

## TL;DR

* **Use a synchronous Havok ray.** Call `hknpWorld::castRay` = `0x140E566A0(hknpWorld*, const hknpRayCastQuery*, hknpCollisionQueryCollector*)` **[V]**. Build three objects on the stack: the query, an `arkCollisionQueryFilter`, and an `hknpClosestHitCollector`. The game does exactly this in the Kiscule `RayTrace` script node (`0x1408E6C70`), in the grapple sensor (`0x140466650`), in the Timepiece (`0x140511FE0`) and in about 30 other places **[V]**.
* **World pointer.** `hknpWorld* = *(game + 0x60410)`, where `game = *(0x142710D68)` **[V live]**. The game's `arkPhysicsWorld` (vtable `arkPhysicsWorldLocal` `0x141EF2D30`) is embedded at `game + 0x60280`. Its field `+0x190` holds the same `hknpWorld*` **[V live]**.
* **Filter = layer mask plus a "must collide with" mask.** The filter accepts a body or shape when two things hold. First, its layer (`info & 0x1F`) is in `mask`. Second, `(info >> 5)` contains every bit of `required` **[V]**.
  * Far Reach uses `mask = 0x20822B`, `required = 0x40`. The mask covers WORLD_LOW, WORLD_LOW_HIGH, RIGID_BODY, MOVER, NPC, INTERACTABLE and CHAIN. `required = 0x40` means the target must block the PLAYER. The player's own layers (6, 7, 8, 18) are never in these masks, so the ray never hits the player **[V]**.
* **See-through materials are flagged and readable.** The game resolves the hit's physics material with `0x1400C63E0(arkPhysicsWorld*, u16* outMatId, bodyId, shapeKey)`. It then reads the `arkPhysicsMaterial` decl from `hknpWorld+0x988 → +0x30 → [id*0x70 + 0x60]` **[V live]**.
  * The decl has a flag byte at `+0x80`: bit 2 = `m_canBlinkThrough` (glass, wooden blinds), bit 3 = `m_canGrappleThrough` (metal grids/grates), bit 4 = `m_canWindblastThrough` **[V]**.
  * Far Reach skips hits whose material has bit 3 set: it targets through grates **[V]**. Blink treats a hit as see-through when the material has bit 2 set **and** the entity has `arkCpntBreakable` **[V]**.
* **Static level geometry is identifiable.**
  * Static level geometry bodies have `userData → arkCpntPhysicsWorld` (vtable `0x141EF2C58`), owned by `idWorldspawn`, on layers 0/1/2 **[V live]**.
  * Static props (static `arkCpntRigidBody`) are on layer 0.
  * Doors, movers, breakables and usables are on layer 5 (MOVER). Dynamic props are on layer 3 (RIGID_BODY) **[V live]**.
* **Nothing in the game checks "flat area for a large shape".** The closest existing checks are listed in section 5. The recommended 9 to 25 ray sequence is in section 6.

---

## 1. The ray API

### 1.1 Functions

| Address | What | Signature / notes |
|---|---|---|
| `0x140E566A0` | `hknpWorld::castRay` **[V]** (timer string `"TtWorldCastRay"`) | `void (hknpWorld* w, const hknpRayCastQuery* q, hknpCollisionQueryCollector* c)`. Calls `(*(w+0x478))->vt[0x88/8](disp, q, *(w+0x28) /*bodies*/, c)`. Returns nothing; the results are in the collector. |
| `0x140E56760` | `hknpWorld::castShape` **[V]** (`"TtWorldCastShape"`) | `void (w, const hknpShapeCastQuery* q, const hkRotation* shapeOrient, collector* c, collector* startPointCollector /*nullable*/)`. See 5.3. |
| `0x140E56920` | `hknpWorld::getClosestPoints` **[I]** (`"TtWorldGetClosestPoints"`) | Not traced. |
| `0x140E56050` / `0x140E56850` | `queryAabb` / `pointQuery` **[I]** (timer strings) | Not traced. |
| `0x1400C63E0` | resolve leaf material **[V]** | `u16* (void* arkPhysicsWorld /*game+0x60280*/, u16* outMaterialId, u32 bodyId, u32 shapeKey)`. Decodes the shape key through the world's shape tag codec (`w+0x9D8`) and returns `outMaterialId`. Used by Far Reach, Blink, wall sensors and impacts. Read-only. |
| `0x140E54A10` | sort an `hknpAllHitsCollector` by fraction **[I]** | `void (collector*)`. Called after all-hits casts (Kiscule node). |
| `0x1409FAAC0` | `GetComponent(entity, typeDesc, 2)` (known) | Type descriptors used below: `0x143C1E820` = `arkCpntBreakable`, `0x143C28CF0` = `arkCpntPlayerPhysics`, `0x143C28C60` = `arkCpntPlayerCamera` **[V live: name strings in the descriptors]**. |

Asynchronous `arkPhysicsWorld` queries also exist **[V]**. Avoid them for the mod:

* Submit functions: `0x1400C41C0` (ray, with an optional skip-component argument), `0x1400C4590`…`0x1400C4910` (others).
* Fetch results: `0x1400C49F0(arkPW, &id, &hkArray<result>)`. Release: `0x1400C5120(arkPW, &id)`.
* Request slots: `arkPW + 0x8280 + (id & 0x1FFF) * 0x140`.
* Submitting fatal-errors (`"Collision query done outside valid range !"`) unless `arkPW+0x148280 != 0`.
* Results arrive in a later frame.

Far Reach's main aim uses these async queries. Its synchronous `castRay`/`castShape` calls happen inside the same job.

### 1.2 `hknpRayCastQuery` (0x70 bytes, 16-byte aligned)

**[V]** from `0x1408E6C70`, `0x140466650`, `0x1400C5890` and `0x1404CA250`. Every game call site fills it identically.

| Off | Type | Value the game writes |
|---|---|---|
| +0x00 | `hknpShapeTagCodec*` | `*(hknpWorld + 0x9D8)` (vtable `0x141EF2BD8` live) |
| +0x08 | `hknpCollisionFilter*` | `&filter` (1.3) |
| +0x10 | u16 filterData.materialId | `0xFFFF` |
| +0x14 | u32 filterData.collisionFilterInfo | `0` |
| +0x18 | u64 filterData.userData | `0`. With the SkipComponent filter this is the `arkCpnt*` whose bodies are ignored. |
| +0x20 | u32 | `2` (every call site; probably level of detail **[?]**) |
| +0x30 | hkVector4 ray origin | xyz, w = 0 |
| +0x40 | hkVector4 ray direction | xyz = direction, **w = maximum fraction**. The game passes either a unit direction with w = length in m, or `end - start` with w = 1. The result fraction uses the same scale. With a unit direction, fraction = distance in metres. |
| +0x50 | hkVector4 inverse direction | `1/d` per component. A zero component becomes `0x7F7FFFEE` (≈3.4028e38). **w (as u32)** = `(movmskps(d >= 0) & 7) \| 0x3F000000` |
| +0x60 | u32 flags | `0` (two-sided triangle hits **[?]**: Havok `hkcdRayQueryFlags`) |

### 1.3 `arkCollisionQueryFilter` (0x28 bytes)

vtable `0x141E349B8` **[V]**.

| Off | Value | Meaning |
|---|---|---|
| +0x00 | `0x141E349B8` | vtable. `arkCollisionQuerySkipComponentFilter` = `0x141E34798`. |
| +0x08 | `0` (u64) | `hkReferencedObject` extra pointer. Must be 0: the destructor frees it. |
| +0x10 | `0x1FFFF` (u32) | refcount/flags (copy verbatim) |
| +0x18 | `4` (u8) | `hknpCollisionFilter::m_type` = USER |
| +0x20 | u32 **layerMask** | bit L = accept targets on layer L |
| +0x24 | u32 **required** | target's `(collisionFilterInfo >> 5) & required` must equal `required` |

Filter code **[V]**:

* `0x1400C0350` (body test) is `slot 6`: `((mask >> (info&31)) & 1) && ((info>>5) & req) == req`, where `info = body+0x6C`.
* `0x1400C0380` (slot 4) runs the same test on per-shape filter info: it reads the codec-decoded leaf info, so compound children are filtered individually.
* `0x1400C03C0` (SkipComponent variant) also rejects bodies whose `*(*(body+0x90)+8) == query.userData`.

`collisionFilterInfo` layout **[V]**: bits 0..4 = layer, bits 5..31 = the set of layers it collides with. The world pair filter `0x1400C04A0` lets A and B collide when each one's mask contains the other's layer.

After the cast, the game sets vtable `hknpCollisionFilter::vftable` `0x141CE1D88` and calls `0x140C6A200` (the `hkReferencedObject` destructor). With +0x08 = 0 that frees nothing, so the mod can skip it **[I]**.

### 1.4 Collectors

Use `hknpClosestHitCollector`, size 0xA0, vtable `0x141CE6AD8` **[V]**:

| Off | Init | Meaning |
|---|---|---|
| +0x00 | vtable | ClosestHit `0x141CE6AD8`. AnyHit `0x141E34A00` (set +0x08 = 1). AllHits `0x141CE42D0` (different tail, below). |
| +0x08 | u32 0 | hints (AnyHit uses 1) **[I]** |
| +0x0C | u32 0 | **number of hits** (`!= 0` means hit) **[V]** |
| +0x10 | u8 0 | |
| +0x20 | hkVector4 `0x5F7FFFF0` ×4 (≈1.8e19) | early-out fraction (`DAT_1424295D0`) |
| +0x30 | `hknpCollisionResult` (0x70) | the closest hit. Init: fraction (+0x50) = `0x7F7FFFEE`; both BodyInfos {bodyId `0xFFFFFF`, materialId `0xFFFF`, shapeKey `0xFFFFFFFF`, cfi 0, +0x10 `0xFFFFFFFF`, userData 0}; +0x98 = 0. Reset function: `0x1400C0290`. |

`hknpAllHitsCollector` (as in Kiscule `0x1408E6C70` and grapple `0x140462D80`):

* Same +0x00..+0x2F header.
* `+0x30` hits pointer, `+0x38` int count, `+0x3C` capacityAndFlags = `0x8000000A`.
* `+0x50` inline storage for 10 results (0x460 bytes).
* If a cast produces more than 10 hits the array moves to the heap. The game then frees it: `h = 0x140C694C0(); h->vt[0x20/8](h, ptr, (cap & 0x3FFFFFFF) * 0x70)` when `(int)cap >= 0`.

Prefer ClosestHit for the mod: it never allocates.

### 1.5 `hknpCollisionResult` (0x70)

**[V]** layout. Its offsets are cross-checked in the Kiscule node, the grapple functions `0x140465500`/`0x140465940`, the Blink function `0x14073C330` and the ark wrapper `0x1400C5890`.

| Off | Field |
|---|---|
| +0x00 | hit position (hkVector4). The grapple reads it **[V]**. You can also compute `origin + dir*fraction`. |
| +0x10 | hit normal (world space, unit) **[V]** |
| +0x20 | **fraction** (float; in direction-length units, see 1.2) **[V]** |
| +0x28 | query BodyInfo (0x20) |
| +0x48 | **hit bodyId** (u32, low 24 bits index the body array) **[V]** |
| +0x4C | hit materialId u16 **[?]**: the game does not read it. It always re-resolves with `0x1400C63E0`. |
| +0x50 | **hit shapeKey** (u32), passed to `0x1400C63E0` **[V]** |
| +0x54 | hit shape collisionFilterInfo **[?]** |
| +0x60 | **hit body userData** (pointer, same as `body+0x90`) **[V]** |

### 1.6 From a hit to body, entity, layer and material

**[V]**, checked live on the current level:

```
bodies = *(u64*)(hknpWorld + 0x28);                    // hknpBody[], stride 0xB0
body   = bodies + (hit.bodyId & 0xFFFFFF) * 0xB0;
  body+0x60 hknpShape*   body+0x6A u16 materialId (body level)
  body+0x6C u32 collisionFilterInfo (layer = &0x1F)    body+0x90 userData
ud   = *(u64*)(body + 0x90);   // arkBodyUserData / havokUserData@arkCpntRigidBody / havokUserData@arkCpntPhysicsWorld
cpnt = *(u64*)(ud + 8);        // arkCpntPhysicsWorld (0x141EF2C58) | arkCpntRigidBody | arkCpntPhysicsSystem
ent  = *(u64*)(cpnt + 0x10);   // idWorldspawn for level geometry; entity handle = *(u32*)(ent + 0xA8)
u16 mat; 0x1400C63E0(game + 0x60280, &mat, hit.bodyId, hit.shapeKey);
lib = *(u64*)(hknpWorld + 0x988);  arr = *(u64*)(lib + 0x30);          // hknpMaterial[], stride 0x70
pm  = *(u64*)(arr + mat*0x70 + 0x60);   // arkPhysicsMaterial decl (vtable 0x141E33BA8) or NULL (ids 1..3 are NULL live)
```

The library count is probably `*(int*)(lib+0x38)` (256 live) **[I]**.

Body census on the live level **[V]**, as layer: owner:

* 0 WORLD_LOW: 4 worldspawn compounds, plus about 130 static props (`arkCpntRigidBody` on idEntity/idDynamicEntity/arkPickup, including `env.metal.grid.mantleoff` grates).
* 1 WORLD_LOW_HIGH: 15 worldspawn compounds.
* 2 WORLD_HIGH: 6 worldspawn compounds. These do **not** collide with PLAYER; they do collide with PROJECTILE.
* 3 RIGID_BODY: dynamic props.
* 4 DEBRIS.
* 5 MOVER: arkDoor, arkMover, arkBreakable, arkUsable.

## 2. Layers and masks

Collision layer enum (table at `0x1424C1890`) **[V]**:

```
0 WORLD_LOW  1 WORLD_LOW_HIGH  2 WORLD_HIGH  3 RIGID_BODY  4 DEBRIS  5 MOVER  6 PLAYER  7 PLAYER_HITBOX
8 PLAYER_DEAD  9 NPC  10 NPC_BODY_ANIMATED  11 PROJECTILE  12 CLOTH  13 CLOTH_COLLIDER  14 NPC_BODY_RAGDOLL
15 INTERACTABLE  16 TRIGGER  17 VISION  18 PLAYER_PHYSICS  19 DECAL_QUERY  20 WEAPON_MELEE  21 CHAIN
22 TRIGGER_WATER  23 PICKUP  24 (unused)  25 GHOST  26 HIDDEN
```

Reading the layers:

* WORLD_LOW is the simplified player collision.
* WORLD_HIGH is detail collision for projectiles.
* WORLD_LOW_HIGH is shared by both.

This is consistent with the masks the game uses and with the live body masks **[I]**.

`arkCollisionSettings_t` block flags (`m_blockFlags` in physicsmaterial decls) **[V]**: NPC 1, PLAYER 2, WEAPON 4, VISION 8, DYNAMIC 16, BLOCKED_BY_STATIC 32, DEBRIS_AND_PARTICLES 64, RAGDOLL 128, DEFAULT 127.

Masks used by the game **[V]**:

| User | mask | required | |
|---|---|---|---|
| Far Reach sensor (`arkDeclCpntPlayerSensorGrapple::m_collisionLayers` +0xC0, ctor default `0x140460F70`) | `0x20822B` | `0x40` (PLAYER) | layers 0,1,3,5,9,15,21 |
| Timepiece player-fits test (`0x140511FE0`) | `0x22B` | `0x40` | 0,1,3,5,9 |
| Kiscule `RayTrace` node | `0x7FFFFFF` or `7` (bool input) | `0x20000` (VISION) | |
| Line-of-sight `0x1404CA250` | `7` | `0x20000` | |
| Projectile-style queries (`0x14075B820`, `0x1407EE630`, `0x14056AB60`…) | `0x2F`/`0x26`/`0x2E` | `0x800` (PROJECTILE) | |
| AI/NPC queries (`0x140871EE0`, `0x140872140`) | `3`/`0x23` | `0x200` (NPC) | |

## 3. Surface material and see-through detection

### 3.1 The `arkPhysicsMaterial` decl

Type info is registered at `0x14176E7C0` **[V]**.

Fields:

* +0x58 Density, +0x5C DynamicFriction, +0x60 StaticFriction, +0x64 Restitution.
* +0x68 `m_collisionSettings` (0x10).
* +0x78 `m_projectileContactSettings`.
* **+0x7C `m_allowedWallSensors`**: OTHER 1, SHADOW_WALK 2, MESMERIZE 4, **GRAPPLE 8**; default 0xF.
* **+0x80 flag byte**: bit0 allowMantle, bit1 allowVault, **bit2 canBlinkThrough**, **bit3 canGrappleThrough**, bit4 canWindblastThrough, bit5 triggerMaterial, bit6 triggerTypeContactSolver, bit7 isStairs. The bit accessors are at `0x140B69C70..0x140B69DC0`.
* +0x84 editorColor.

Live values **[V]**:

| Material | +0x80 |
|---|---|
| `default_preset` | 0x03 |
| `env.glass` | 0x07 |
| `po.wood.blind` | 0x07 |
| `env.metal.grid` | 0x1B |
| `env.metal.grid.rbsimple` | 0x1B |
| `env.metal.grid.mantleoff` | 0x18 |
| `env.metal.grid.rbsimplemoff` | 0x18 |
| `env.metal.grid.rbweapon` | 0x13 |

`+0x7C` is 0xF everywhere. No stock decl sets `m_allowedWallSensors`.

Stock decls with see-through flags:

* `canBlinkThrough`: `env.glass`, `po.wood.blind`.
* `canGrappleThrough`: `env.metal.grid` and its children, except `rbweapon`.
* Grid variants `rbsimple*` use `m_blockFlags = NPC PLAYER DYNAMIC BLOCKED_BY_STATIC DEBRIS`. They do **not** block WEAPON or VISION, so bullets and sight pass through them.

### 3.2 What material and flags do **not** tell you

* There is **no "alpha-tested / two-sided / nonsolid" flag on the physics side.** Render materials (`idMaterial`) are not linked to Havok hits. A foliage card with no collision is simply not hit.
* A railing or fence modelled as a **solid box in the player collision** (WORLD_LOW) still looks solid to a player-solid ray. The only physics-side signal is the difference between the player and projectile collision sets. See check C in section 6 **[?]**: not yet tested in game.

### 3.3 How the game rejects targets

**Far Reach.** The target is picked by `0x140465500(sensor, hits, n, maxDist, &outPos, &outNormal, &outEntity, bool, bool)` **[V]**. It walks the sorted hits and skips some entity kinds. For each hit it resolves the material with `0x1400C63E0`:

* `pm+0x80 & 8` (canGrappleThrough): the hit is **ignored**. The grapple targets through the grate, and the sensor sets `sensor+0x208 = 1` ("went through a grate").
* Otherwise the first hit closer than `maxDist` wins. Output position = `hit.pos + hit.normal * decl.m_shapeRadius` (0.01), so the sensor point is about 1 cm off the surface.
* At the end, if `!(pm+0x7C & 8)`, the entity output is cleared.

`0x140465940(…, hits, n, maxDist, ignoreEnt)` returns true if every hit closer than `maxDist` is grapple-through or one of the skipped kinds: a "path clear" test **[V]**.

**Blink.** `0x14073C330(hknpCollisionResult*)` returns true ("see-through") if `pm+0x80 & 4` **and** `GetComponent(ent, arkCpntBreakable 0x143C1E820, 2) != 0` **[V]**. It is used by the Blink and blink-snap sensors (`0x140467840`, `0x140468490`, `0x140478440`, `0x140486A70`, `0x140739AD0`).

**Grapple component → +0xF0/+0xFC.** In `0x140501B00` **[V]**:

* `+0xD7` = `sensor->vt[0xD0/8]()`, which returns `sensor+0x1FE`.
* `+0xF0` = `sensor+0x1CC` (hit point, 1 cm off the surface).
* `+0xFC` = `sensor+0x1D8` (normal).
* The sensor is `*(*(owner+0x338)+0xF8)`.
* With no valid target, `+0xF0` is the point at maximum range in the air and `+0xFC` is the owner's axis.
* In the mantle case, `+0xFC` = up (`DAT_1424F67E0`).

`+0x108` (the FX position, `0x1405047A0`) equals `+0xF0` for an ordinary wall. It is modified for ceilings and floors and can be lowered by `+0x150`. **Use `+0xF0` as the surface point.**

## 4. Existing working example (trimmed)

Kiscule `RayTrace` node, `0x1408E6C70` **[V]**:

```c
lVar16 = *(game + 0x60410);                          // hknpWorld
q.codec = *(lVar16 + 0x9d8); q.filter = &f; q.matId = 0xffff; q.cfi = 0; q.ud = 0; q.lod = 2;
q.origin = start; q.dir = end - start (w = 1)  |  forward (w = length);  q.invDir = rcp, w = signmask|0x3f000000; q.flags = 0;
f.vt = arkCollisionQueryFilter; f[+8]=0; f[+0x10]=0x1ffff; f[+0x18]=4; f.mask = staticOnly ? 7 : 0x7ffffff; f.required = 0x20000;
c.vt = hknpAllHitsCollector; c.data = inline; c.capFlags = 0x8000000a; c.earlyOut = DAT_1424295d0 x4; c.count = 0;
FUN_140e566a0(lVar16, &q, &c);
if (c.count > 0) { FUN_140e54a10(&c);                // sort
  for (hit in c) { ent = *(*(*(bodies + (hit.bodyId & 0xffffff)*0xb0 + 0x90) + 8) + 0x10);
    if (ent != source && ent != target) { out.entity = ent->handle(+0xa8); out.dist = |dir| * hit.fraction; break; } } }
```

## 5. Similar checks already in the game

1. **Far Reach.** Section 3.3: material see-through skip, wall-sensor flag, and snap logic (`m_maxAngleSnapping 8°`, `m_maxDistanceSnapping 0.25`). It never checks the area around the hit.
2. **Blink.** The glass/breakable see-through rule in 3.3. The Blink-to-mantle and grapple-to-mantle sensors (`m_wallAngleMax 40`, `m_forwardCastHeightMax 2.5`) are async; they were not traced.
3. **Timepiece "player fits" test, `0x140511FE0`** **[V]**. This is a zero-length `castShape` of the player's current stance shape:
   * The shape is `*(*(pp+0x10) + idx*0xB8 + 0x18)`, where `pp = *(GetComponent(player, 0x143C28CF0, 2) + 0x48)` and `idx = *(int*)(cpnt + 0x58 + *(int*)(cpnt+0x50)*4)` **[I]**.
   * Query: ray query layout plus `+0x28 hknpShape*`, `+0x60 float 0.001`, `+0x64 0`, `+0x68 0`. Origin = test position. Direction = 0 with w = 1.0.
   * Orientation = identity at `0x142429580`. Filter mask `0x22B`, required `0x40`. Collector: ClosestHit.
   * Any hit means the player does not fit. It retries 0.2 m higher until it runs out.
   * **Reusable** to check that the player can exit in front of a gate.
4. **Wall sensor** `0x1404838B0` (arkCpntPlayerSensorWall) **[I]**. Async rays; it checks `pm+0x7C` bit `sensorType` (`bt` at `0x140484277`).
5. **Not physics-based.** Doppelganger and Mesmerize placement use navmesh sensors. `arkCpntPlayerSensorPlacement`'s decl is empty. No decal "flat area" test was found (not searched in depth).

## 6. Recommended sequence: "is this a solid flat surface for a 1.7 × 2.8 m oval"

Where to run it: in the **grapple component update hook** (`0x140500CE0`, called from job `0x140B06290`). That is the same phase in which the grapple sensor job (`0x140B05BE0` → `0x140462D80`) runs its own synchronous `castRay`/`castShape` **[I]**.

Do **not** run it from the window thread or the module's own thread. Havok queries may use the thread's Havok memory router. Prefer not to run it from the player physics hook either.

```cpp
struct alignas(16) RayQuery { void* codec; void* filter; uint16_t matId, _p0; uint32_t cfi; uint64_t ud;
    uint32_t lod, _p1[3]; float org[4], dir[4], inv[3]; uint32_t signW; uint32_t flags, _p2[3]; };   // 0x70
struct alignas(16) QFilter { const void* vt; uint64_t ref; uint32_t sz, _p; uint8_t type, _p2[7]; uint32_t mask, req; }; // 0x28
struct BodyInfo { uint32_t body; uint16_t mat, _p; uint32_t key, cfi, unk, _p2; uint64_t ud; };          // 0x20
struct alignas(16) HitResult { float pos[4], nrm[4]; float frac; uint32_t _p; BodyInfo q, hit; uint64_t tail; }; // 0x70
struct alignas(16) Closest { const void* vt; uint32_t hints, nHits; uint8_t b10, _p[15]; float early[4]; HitResult r; }; // 0xA0

bool CastClosest(const float o[3], const float d[3] /*unit*/, float len, uint32_t mask, uint32_t req, HitResult& out) {
    uintptr_t game = *(uintptr_t*)0x142710D68; if (!game) return false;
    void* w = *(void**)(game + 0x60410);       if (!w) return false;           // loading / no world
    QFilter f{ (void*)0x141E349B8, 0, 0x1FFFF, 0, 4, {}, mask, req };
    RayQuery q{}; q.codec = *(void**)((uintptr_t)w + 0x9D8); q.filter = &f; q.matId = 0xFFFF; q.lod = 2;
    for (int i = 0; i < 3; i++) { q.org[i] = o[i]; q.dir[i] = d[i] == 0.f ? 0.f : d[i];
        q.inv[i] = d[i] == 0.f ? 3.40282e38f /*0x7F7FFFEE*/ : 1.f / d[i]; }
    q.dir[3] = len; q.signW = (d[0] >= 0) | (d[1] >= 0) << 1 | (d[2] >= 0) << 2 | 0x3F000000;
    Closest c{}; c.vt = (void*)0x141CE6AD8; for (float& e : c.early) e = 1.8446e19f /*0x5F7FFFF0*/;
    c.r.frac = 3.40282e38f; c.r.q = c.r.hit = { 0xFFFFFF, 0xFFFF, 0, 0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 0 };
    ((void(*)(void*, const void*, void*))0x140E566A0)(w, &q, &c);
    if (!c.nHits) return false; out = c.r; return true;                          // out.frac = metres (unit dir)
}
```

Masks:

* `M_PLAYER = 0x20822B, R_PLAYER = 0x40`: what Far Reach sees.
* `M_PROJ = 0x6, R_PROJ = 0x800`: detail collision.

### Inputs

* `P = grapple+0xF0` and `n = normalize(grapple+0xFC)`.
* Require `+0xD7 != 0`, `+0xD8 == 0`, and `+0xDC == 0x3FFF0010` (no entity target).
* Build the tangent basis `t` (horizontal, `= normalize(cross(up, n))`) and `b = cross(n, t)`. For floors and ceilings (`|n.z| > 0.9`), use the player's forward axis projected onto the plane.
* Semi-axes `a = 0.85`, `c = 1.40` (half of 1.7 × 2.8).

### A. Center ray

* origin `P + n*0.10`, dir `-n`, len `0.30`, `M_PLAYER/R_PLAYER`.
* Must hit at `frac ≈ 0.11 ± 0.05`.
* Hit layer `(body+0x6C) & 0x1F` must be ≤ 2 (static world or static prop; rejects doors, movers, breakables, dynamic props, NPCs).
* `pm` flags: reject `pm+0x80 & (0x04|0x08|0x10|0x20)` (blink-through, grapple-through, windblast-through, trigger).
* Optional: reject when `GetComponent(ent, 0x143C1E820)` (breakable).
* Optional: reject when `pm+0x7C & 8` is clear (the same rule Far Reach uses).
* Plane: `n0 = hit normal` (require `dot(n0, n) > 0.94`), `d0 = dot(n0, P + n*0.10 - n*frac)`.

### B. Support rays

* 8 rim points at 85 % (`P + t*a*cos θ + b*c*sin θ`, θ = k·45°), plus 4 points at 45 % if wanted.
* Same ray as A from `point + n*0.10`.
* Each must hit, pass the same layer and material rules, have `dot(normal, n0) > 0.966` (15°), and have `|dot(n0, hitPoint) - d0| < 0.03`.
* Any miss means the oval hangs in the air. Any depth or normal mismatch means a step, corner, pipe or uneven ground.

### C. Holey or proxy-collider test **[?]**

Recast A and the B points with `M_PROJ/R_PROJ`. If the player ray hit at depth `t` but the projectile ray misses, or hits deeper than `t + 0.05`, the player collider is a simplified blocker. Typical cases: a box around balusters, a fence, a hedge, an invisible wall. Reject.

This rests on live data, not on a test of real railings yet: WORLD_HIGH bodies collide with PROJECTILE and not PLAYER, WORLD_LOW the reverse. Log the results before enforcing the rule.

### D. Clearance in the plane (cheap, recommended)

* From `P + n*0.05`, cast toward each rim point extended 5 % (dir = unit, len = distance), `M_PLAYER/R_PLAYER`.
* Any hit means a perpendicular wall, a door frame or an object crosses the oval. This catches inside corners, which section B can miss when a rim point starts inside another wall.

### E. Optional: player exit space

Use the Timepiece shape cast (5.3) at `P + n*(capsuleRadius + 0.05)` for walls. For floor or ceiling gates, use the stand position. Mask `0x22B`, required `0x40`. Any hit means the player cannot exit there.

Cost: about 25 rays and one optional shape cast, only on a placement key press. That is negligible.

## 7. Risks

* **Threading.** Run only on game job threads, during component updates; that is where the game's own sensors cast. The world can be absent during loads: check `*(game+0x60410)`.
* **Body array.** It may be reallocated when bodies are spawned. Re-read `w+0x28` for every cast; never cache body pointers.
* **Alignment.** Query and collector must be 16-byte aligned (SSE loads). Use `alignas(16)` locals, not packed structs.
* **NULL materials.** Material ids 1..3 are NULL live. Check `pm != 0` and `*(u64*)pm == 0x141E33BA8` before reading `+0x80`.
* **Start inside geometry.** A ray that starts inside solid geometry (rim points in a corner): two-sided mesh hits make it report about 0 distance, and convex shapes report nothing **[?]**. Check D covers this.
* **The +0x20 = 2 and +0x60 = 0 query fields** are copied from the game without full understanding. Do not change them.
* **The 10 cm start offset.** Grapple `+0xF0` is the 1 cm sphere centre, not the contact point; the center ray in check A gives the real plane.
* **Gate entities.** `voidgate_gate*.def` (idEntity + cpntFX) presumably have no physics bodies, so they will not block the rays **[?]**.

## 8. Open questions

1. Is result `+0x4C` (materialId) / `+0x54` (shape filter info) filled by `castRay`? Log it once next to the `0x1400C63E0` result. If it is filled, the per-hit layer is `+0x54 & 0x1F`.
2. Check C needs in-game tests. Rays at a balustrade, a metal fence, a hedge and a plain wall: log the player depth against the projectile depth.
3. Whether `castRay` is safe from the player physics hook (`0x140AFCA91`). This was not traced; avoid it.
4. The `hknpCollisionQuery` +0x20 meaning (level of detail?) and the result +0x58 / +0x68 fields.
5. The exact async query that produces Far Reach's main target (`sensor+0x1BC/+0x1C0` handles, submitted outside `0x140462D80`). It is not needed for the mod.

## 9. Address index

| Address | What |
|---|---|
| `0x142710D68` | game global; `+0x60280` arkPhysicsWorld(Local), `+0x60410` hknpWorld* |
| `0x140E566A0` / `0x140E56760` | hknpWorld castRay / castShape |
| `0x1400C63E0` | resolve leaf hknpMaterialId (arkPW, &out, bodyId, shapeKey) |
| `0x1400C5890` | arkPhysicsWorld sync ray executor for async requests (closest/all/any) |
| `0x1400C49F0` / `0x1400C5120` / `0x1400C41C0` | async query fetch / release / submit ray |
| `0x141E349B8` / `0x141E34798` | arkCollisionQueryFilter / …SkipComponentFilter vtables |
| `0x141CE6AD8` / `0x141CE42D0` / `0x141E34A00` | ClosestHit / AllHits / AnyHit collector vtables |
| `0x141CE41E8` / `0x141CE1D88` | hknpCollisionQueryCollector / hknpCollisionFilter vtables (destructor path) |
| `0x1400C0350` / `0x1400C0380` / `0x1400C03C0` | filter body test / shape test / skip-component body test |
| `0x141E33BA8` | arkPhysicsMaterial vtable; type info `0x14176E7C0` |
| `0x141EF2C58` | arkCpntPhysicsWorld vtable (static level body owner) |
| `0x140465500` / `0x140465940` | Far Reach target pick / path-clear (material bit 3) |
| `0x14073C330` | Blink see-through test (material bit 2 + arkCpntBreakable) |
| `0x140466650` | grapple vertical up/down closest-hit rays (example) |
| `0x140462D80` | arkCpntPlayerSensorGrapple update (job `0x140B05BE0`) |
| `0x140501B00` | grapple component: copies sensor hit to +0xF0/+0xFC, sets +0xD7/+0xD8 |
| `0x1408E6C70` | Kiscule RayTrace node execution (all-hits example) |
| `0x140511FE0` | Timepiece player-fits shape cast |
| `0x140460F70` | arkDeclCpntPlayerSensorGrapple ctor (m_collisionLayers default 0x20822B) |
| `0x1424C1890` / `0x1424C0BC0` | collision layer enum / block flags enum tables |
