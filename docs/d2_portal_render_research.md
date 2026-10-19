# D2 "see-through portal" rendering: how the Timepiece does it, and how Void Gate can reuse it

## Implementation status (2026-10-13, tested in game)

Working (native_d2/vg.c "see-through window", host hook on the 3 calls of 0x140A393A0, data from
tools/make_voidgate_view.py):
* view[1] = the player's renderView copied, origin/axis carried through the gates with vg_portal.h; our own scratch
  image `_VoidGatePortal`; rs->vt[0x1B8] armed every frame after the main view submit; the parm `rendering/portal`
  bound on the window entity's render entity (+0x1950). Verified by screenshots: the window shows the other gate's side.
* Window entity: Delilah's portal plane (portal.md6, skinned -> the stock lens permutation, no freeze) with a copy of
  portal.material. Gotchas: (1) `tsd/FailFactor` is set only by the Timepiece's code; unset it acts as 1 and
  portalFactor = the failure-noise map (a shard pattern) -> the copy uses `models/effects/textures/test/white.tga` as
  the noise map; (2) the spawn keeps the axis orthonormal, but writing scaled rows into entity +0x30C afterwards scales
  the render (used to size it); (3) reshaping the plane's vertices into an oval (positions: 436 x float3 from offset
  282, two layers x = 0 / 0.004) made shards with holes. The shipped window: the stock rectangle drawn 10 times with a
  sheared axis (entity +0x30C rows are used as a raw matrix: any parallelogram works). Piece i has the corners
  v_i, v_i+1, -v_i, -v_i+1 of a 20-gon inscribed in the oval at 0.93, so the pieces cover the polygon; overlaps don't
  show (screen-space sampling). 2 cm in front of the gate FX. (Before: 3 stacked rectangles, visible steps.)
* Cut at the exit surface: 0x140409D2F NOPed (module), explicit oblique projection (main projection is reversed-Z
  INFINITE far: rows [a 0 0 0][0 b 0 0][0 0 0 n][0 0 -1 0]), plane 16 cm in front of gate B (its own FX goes too).
  Works with the whole viewport as the rect; with a scissor rect around the window the clipped view came out BLACK
  (unexplained: maybe the subrect projection is derived differently for explicit projections) -> full-screen second
  render while clipping (performance cost: a second full scene render each frame).
* One live window at a time (2026-10-13 evening): on the gate whose oval is on screen, seen from its front, that the
  player is nearer to (sticky by 2 m, 500 ms between switches); spawned/removed from the player physics frame hook.
  The second view is armed only while that window is on screen and seen from the front: before, an unseen window cost
  a whole second scene render. Measured (atrium, `d2test.py fps`): no gates 41 fps, window in view 35.5, window off 41.
* Tried and dropped: a window on each gate taking turns (one extra view a frame, each window keeping its last picture,
  one image per window). In motion each window shows a picture from another frame: it flickers (user report). Two
  renders a frame would double the cost. So both windows live at once is not done.
* Not the window but the module cost frame rate for a while: a per-tick entity loop called VirtualQuery for every
  entity near the gates (thousands of system calls a frame). Fixed (no VirtualQuery there, once per frame).
* Test channel: viewon / viewclip 0|1|2 / viewclipoff / viewrect / viewout / viewfit / frames; scenario
  `py tools\d2test.py surfaces` (screenshots of several exit-gate placements -> scratch/surfaces.png).

Open: recursion (a gate seen through the other one), the scissor rect with clipping, the dark lower parts
seen in some placements (real shadow or depth?), both gates live at once, recursion, performance.


Dishonored 2 GOG 1.77.9.0, `Dishonored2.exe`, image base 0x140000000 (no ASLR). Research date 2026-10-13.
This was read-only work with Ghidra (gdec), x64refs/capstone and the stock decls. Nothing was tested in game.

Legend: **[V]** = seen in code or data. **[I]** = inferred from code but not fully traced. **[?]** = guess or open question.

---

## 0. Summary

* The engine has exactly **one** extra-view mechanism, the "portal". Only `arkCpntTimeShiftDevice` uses it **[V]**. There
  are no mirrors, remote cameras or `_scratch` subviews in the engine or the stock decls. `cpntRenderView` is a
  full-screen camera override (killcams, freecam, midnight cams), not render-to-texture **[V]**.
* The pieces:
  1. **Render world** `*(G+0x1E8940)` (idRenderWorldLocal) owns **exactly 2 `idRenderView`s** (0xBD00 bytes each).
     View[0] is the main view and view[1] is the portal view **[V]**.
  2. **Render system** `0x14359FB20` (idRenderSystemLocal) has a single "pending portal" slot that is armed each frame by
     `vt[0x1B8]` = `0x140409A10` `SetPortalView(image, idRenderView*, scissorRect, clipPlane)`. The backend consumes it
     **once per frame, before the main view**, in `vt[0x1C8]` = `0x140409AA0` `RenderPortalView` **[V]**.
  3. The portal view is a full second scene render: the main viewport, a scissor limited to the lens rect, and output to
     the RT `_TimeTravelPortal` (full render resolution) instead of the backbuffer **[V]**.
  4. The lens material samples `$(rendering/portal)` in **screen space**. The RT is bound per render entity through the
     entity's `gameParmBlock` (render entity +0x1950) **[V]**.
* **The camera can have any origin and any rotation.** The game writes `vieworg` (+0xA8) and `viewaxis` (+0xCC, 3x3)
  freely. The Timepiece just copies the player's axis and offsets the origin by a translation **[V]**. FOV is forced to the
  main view's FOV by the backend **[V]**, which keeps screen-space sampling aligned.
* **Clip plane:** `renderView_t.customClipPlane` (+0xF0) exists and is passed through `SetPortalView`. It only feeds
  **Umbra occlusion culling** as a user clip plane (`0x1403AE660` -> `0x140C41D40`); FLT_MAX x4 means "off" **[V]**.
  It does **not** clip rasterization. The projection builder `0x140418970` has no oblique near plane. It does support an
  **explicit projection matrix** (`renderView+0x100`, flag +0x140) **[V]**. The portal path clears that flag at
  `0x140409D2F` (7 bytes, `C6 80 40 01 00 00 00`) **[V]**. If those 7 bytes are NOPed, a mod can supply its own
  oblique-near-plane projection.
* **The minimal plan needs no Timepiece.** A hook around the main-view submit `0x140A393A0` (game thread) does this
  every frame:
  1. Fill view[1] by copying the player's `renderView_t` and overriding origin, axis and projection.
  2. Call `rs->vt[0x1B8]` with our own RT, the gate's scissor rect and the exit-gate plane.
  3. Bind the RT to the gate mesh's `rendering/portal` parm.

  Limit: **one portal view per frame**. Only one gate of the pair can be "live" per frame unless a second pass is added
  (high risk, see 5.6).

---

## 1. The second view: setup, submission, offset, enable conditions

### 1.1 Classes and objects

**`arkCpntTimeShiftDevice`**: vftable 0x141E87E18, ctor 0x14050FC40, init (vt slot 3) 0x14050FFF0. Fields come from
reflection 0x1419114A0 **[V]**:

| off | field | notes |
|---|---|---|
| +0x08 | decl | `arkDeclCpntTimeShiftDevice`; +0x120 m_materialToReplace, +0x128 m_portalMaterial |
| +0x10 | owner entity | the device model entity; `+0xF0` = its render entity |
| +0x68 | **m_portalImage** | `idImage*` = RT `_TimeTravelPortal` |
| +0x70 / +0x80 | m_inactiveMaterials / m_activeMaterials | idList of per-surface materials, for render entity vt[0x60] |
| +0x98 | m_animCpnt | |
| +0xA0 | m_stateMachineCpnt | GetComponent(player, type 0x143C06920) |
| +0xA8 | bitfield | bit0 m_isInPast, bit1 m_portalActivated, bit2 **m_portalMaterialSet**, bit3 m_isUsageActivated, bit4 **m_canTeleport**, bit5 m_wasEquipped, bit6 m_wasUnfolded. Getters 0x140B6C9A0 / 0x140B6F5E0 / 0x140B6F6D0 verified for bits 0, 1, 6; the rest follow in order. |
| +0xAC | m_failFactor | -> parm `tsd/FailFactor` (handle `[0x14254E388]`) |
| +0xD8 | m_idleFx (arkFx) | |
| +0x100 | m_viewPortTags | 6 joint handles `tsd_viewport1..6` |
| +0x140 | m_envAreaQueryId | |
| +0x150 | **m_view** | **`idView`, size 0x250F0** (game-side view object; `renderView_t` at its +0) |

**Game-side `idView`**: ctor `0x140A0EF60`. It is also used by `arkCpntRenderView` (0x14096EE00). Offsets used here **[V]**:
* +0x000: `renderView_t` (size 0x39C0, reflection 0x141770130).
* +0x39C0: byte "view moved, redo visibility". Set by `0x140A0F8D0`.
* +0x39C4: horizontal FOV in degrees. The Timepiece uses it for the rect math **[I]**.
* +0x3E40..: camera offset/kick, copied by `0x140A13240`.
* +0x3EB0: game-side view-projection, computed by `0x140A13450`.
* +0x22DD0: environment parm block.

**Player view**: `holder = list[G+0x1F52B8][count-1]` (count = low 24 bits of `G+0x1F52C0`) and `owner = holder-0x20`.
The player `idView` is `owner+0x30` **[V]**, so:
* `owner+0xD8` = vieworg
* `owner+0xF0` = m_portalViewOrg
* `owner+0xFC` = viewaxis
* `owner+0x80` = settings+4 ("time space")

**`renderView_t`** (reflection 0x141770130) **[V]**:

| off | field | off | field |
|---|---|---|---|
| +0x44 | viewID | +0xC0 | **m_portalViewOrg** |
| +0x48 | m_viewIdStudio | +0xCC | **viewaxis** (idMat3; rows forward, left, up) |
| +0x4C | settings (renderSettings_t, 0x48 B, opaque bits) | +0xF0 | **customClipPlane** (idPlane) |
| +0x9C/+0xA0 | fov_x/fov_y RADIANS | +0x100 | **explicitProjectionMatrix** (idRenderMatrix) |
| +0xA4 | cramZNear (near*0.25) | +0x140 | **useExplicitProjectionMatrix** |
| +0xA8 | **vieworg** | +0x144 | m_umbraOffsetWs (added to the camera position for Umbra) |
| +0xB4 | previousvieworg | +0x15C | forceIdentityViewMatrix |
| | | +0x160 / +0x3C0 / +0xBC0 | parmBlock / m_UberMVGenParmBlock / m_environmentParms |

**Render-side `idRenderView`** (type registered at 0x1416609E3; reflection 0x1417599D0; size 0xBD00) **[V]**:
* +0x0000 m_gameRV (renderView_t). The game writes here.
* +0x39C0 m_parentWorld.
* +0x39D0 m_renderRV. The backend copies m_gameRV into it in `0x140419D00`.
* +0x7390 m_projectionMatrix; +0x7450 m_viewMatrix; +0x7490 m_inverseViewMatrix; +0x7510 m_viewProjectionMatrix.
* +0x7650 m_subrect_projectionMatrix.
* +0x77B8 m_perViewConstantBuffer; +0x7800 m_portalViewConstantBuffer.
* +0x7848/+0x784C m_renderWidth/Height.
* +0x7860 m_viewport; +0x7878 m_unscaledViewport; +0x7890 m_scissor (idScreenRect = 6 x int32).
* +0x78C0 m_resolutionScale.
* +0xBCFC m_justTeleported.

**Render world** `rw = *(G+0x1E8940)` (game vt[0x48] = 0x140A3EEE0) is idRenderWorldLocal (vftable 0x141E515B8):
* vt[0xB0] = 0x1402A1260 `NumViews` (`+0x81190 & 0xFFFFFF`).
* vt[0xB8] = 0x1402A2D20 `SetNumViews`.
* vt[0xC0] = 0x1402A2D70 `GetView(i)` (`*(+0x81188)[i]`).
* The ctor 0x1402A12A0 allocates **two** `idRenderView` (0xBD00, ctor `0x140419140(mem, rw, idx)`). The dtor frees
  them with 0x140419940 **[V]**.
* The Timepiece uses `GetView(NumViews()-1)`, which is view[1] **[V]**.

**Render system** `rs = 0x14359FB20` (object; vptr stored there), idRenderSystemLocal vftable 0x141E704F8 **[V]**:
* vt[0x218]/vt[0x220] = render width/height (`[0x14359E8A0]`/`[0x14359E8A4]`).
* vt[0x1B8] = **0x140409A10 SetPortalView**: `rs+0x88 = image; +0x90 = idRenderView*; +0x98..+0xAC = rect[6]; +0xB0..+0xBC = plane[4]`.
* vt[0x1C0] = **0x140409A90 ClearPortalView** (`rs+0x88 = 0`). Called by the Timepiece on unequip at 0x140510E85.
* vt[0x1C8] = **0x140409AA0 RenderPortalView(rs, frameEntry)**.
* Only the Timepiece calls vt[0x1B8]/vt[0x1C0]: 0x140511E14 and 0x140510E85. A whole-exe scan found no other callers **[V]**.

### 1.2 Per-frame game side: `0x140511580` (Timepiece "render" phase)

Call path: factory `arkCpntTimeShiftDeviceFactory` vftable 0x141F44088, slot 9 (+0x48) = 0x140B01A70. That thunk
iterates the active components and calls 0x140511580. The update phase is slot 4 = 0x140B01A10 -> 0x140511210 **[V]**.

Trimmed decompile, with names added:
```c
void TSD_RenderPortal(TSD *this) {                       // 0x140511580
  idFx_Update(&this->m_idleFx,0,0,0,0);
  if (!(this->bits & 4 /*m_portalMaterialSet*/)) return;
  if (G->vt[0x90](G) != 3) return;                       // game state 3 = in game (G+0x340FA0)
  owner = playerViewList.last - 0x20;                     // G+0x1F52B8
  org = owner->vieworg;  axis = owner->viewaxis;          // +0xD8 / +0xFC
  TSD_OffsetOrigin(this, &org);                           // 0x140510FC0: org +/- timeline offset
  if (this->bits & 0x10 /*m_canTeleport*/)
      idView_SetOrgAxis(&this->m_view, &org, &axis);      // 0x140A0F8D0 (any axis accepted)
  idView_CopyCameraOffsets(&this->m_view, &owner->view);  // 0x140A13240 (+0x3E40..+0x3E90)
  idView_Finalize(&this->m_view, &this->m_view, skipVis); // 0x140A10780: env parms, lens dirt, fog LUT...
  rw = G->vt[0x48](G);
  v1 = rw->vt[0xC0](rw, rw->vt[0xB0](rw) - 1);            // view[1] (idRenderView)
  renderView_copy(v1, &this->m_view);                     // 0x140064A90 = renderView_t::operator=
  v1->settings[+4 => +0x50] = (~bits & 1) * 2;           // "time space" = the other timeline
  owner->view.m_portalViewOrg = org;                      // +0xF0: main view LOD/streaming also uses the portal origin
  copy env parm block this+0x22F20 -> v1+0xBC0;           // m_environmentParms
  v0 = rw->vt[0xC0](rw, 0);                               // main idRenderView (previous frame values)
  if (v0 viewport > 16x16) {
     build main proj from owner FOV(+0x39F4 deg; 9:16 trick; aspect = G[0x681BE]*G[0x681BF]/G+0x340DF4)
     project 6 lens joints (0x1406467E0) -> NDC clamp [-1,1] -> pixel rect in v0->m_unscaledViewport
     if (rect non-empty) {
        int rect[6] = {x1,y1,x2,y2,0,0};  float plane[4] = {FLT_MAX x4};   // clip plane OFF
        rs->vt[0x1B8](rs, this->m_portalImage, v1, rect, plane);           // arm the portal render
     }
  }
  re = owner_entity->+0xF0;                               // device render entity
  if (re) { parm.Remove(re+0x1950, rendering/portal);     // 0x1403DC040, handle [0x14254E368]
            parm.Set   (re+0x1950, rendering/portal, {RT+0x98, 0}); }      // 0x1403DBCF0
}
```

* **`0x140A0F8D0` idView::SetOrgAxis(view, org, axis)** **[V]**: writes `vieworg = m_portalViewOrg = org`, `viewaxis = axis`
  and `+0x39C0 = 1`. It then calls a Havok VDB "player" camera update (harmless) and recomputes the game-side
  view-projection (`0x140A13450`).
* **World offset (timeline translation)** `0x140510FC0` **[V]**:
  `off = *(vec3*)( *( *(GetComponent(*(G+0x312FF8), 0x143C06920, 2) + 0x28) + 0x230) + 0xB0 )`, then
  `org = playerOrg + off` if `m_isInPast`, else `org = playerOrg - off`. It is a pure translation. The rotation is the
  player's axis unchanged.
* **Update phase `0x140511210`** **[V]**:
  * Computes `m_canTeleport` (0x140511FE0: Havok shape casts at the other-timeline position).
  * Sets the main view's settings+4: `owner+0x80 = m_isInPast*2`.
  * Runs the env-area query for the portal camera (`0x140A139F0(&m_view, &m_envAreaQueryId)`) and the env blend
    (`0x140A10FF0(&m_view, envDecl, dt)`). This gives the portal view its own fog and lighting.
  * Animates `tsd/FailFactor`.
  * Returns early before the FailFactor parm when cvar `ark_timeshift_skipportal` (value `[0x143BE0988]`) is set. That
    cvar does **not** stop the render phase **[V]**.

### 1.3 Enable conditions
* The view is armed only while `m_portalMaterialSet` (bit 2 of +0xA8) is set and game state == 3 **[V]**. The bit means
  the lens material has been swapped to `portal.material`, i.e. the device is unfolded **[I]**. Where the bit gets set
  in the player-state code was not traced; it sits in 0x140510A30 blocks that Ghidra marked unreachable.
* The origin follows the player only while `m_canTeleport` is set (bit 4). Otherwise the view keeps the last origin.
* It is cleared on unequip, `0x140510DE0`: clears bits 1 and 2 and calls `rs->vt[0x1C0]` **[V]**.
* There is no cvar that forces the view. The backend renders whenever `rs+0x88 != 0` at frame time **[V]**.

### 1.4 Backend: `0x140409AA0` RenderPortalView (render frame `0x1403FF3E0` calls it at 0x1403FF73F, before the main views) **[V]**
```c
if (!rs->portalImage) return;                       // rs+0x88
main = frameEntry->view;                            // frameEntry+0x28 (idRenderView*)
if (main->m_gameRV.m_viewIdStudio) return;          // +0x48
resize RT to the current output size if needed;     // FUN_14037E5C0
frameEntry->view = rs->portalView;                  // swap in the portal view
pv = rs->portalView;  set renderSettings bits: +0x74|=1, +0x77|=2|4, +0x80&=~7, +0x84=0, +0x76|=0x61,
                      +0x64|=1 (= "is portal view": selects the m_umbraVisibilityInPortal bit), +0x75=0
bind RT as output (FUN_140372A20(RT,0,1,0,1,0,0), FUN_14040E970);
rs->portalImage = 0;                                // consumed: must be re-armed every frame
pv->useExplicitProjectionMatrix(+0x140) = 0;        // <-- 0x140409D2F, the patch point for an oblique projection
pv->customClipPlane(+0xF0) = rs->plane;             // rs+0xB0
copy from main: +0x3C0 UberMVGen parms, +0x860, fov_x/fov_y (+0x9C/+0xA0), +0x868..+0x89C dark-vision parms, +0x160 parmBlock
idRenderView_SetViewport(pv, frameEntry, &main->m_resolutionScale, rs->rect);  // 0x140419A20: viewport = main, scissor = rect
[0x14254DC78] = main->m_resolutionScale;            // -> renderparm rendering/portalResolutionScale (set in 0x1403E56C0)
RenderSingleView(rs+0xD7F0, ..., frameEntry);      // 0x1403E6C60, the whole pipeline
restore output and frameEntry->view;
```
* `0x140419D00` (view setup) copies m_gameRV into m_renderRV, then calls `0x140418970` to build the projection. Behavior:
  * If `useExplicitProjectionMatrix` is set, it copies `explicitProjectionMatrix` verbatim.
  * Otherwise it uses a perspective from fov_x/fov_y plus TAA jitter.
  * The view matrix comes from `0x140154F80(org, axis)` **[V]**.
* Projection convention, from `0x1401552C0` **[V]**:
  * Row-major, column vectors.
  * View space: x = -left, y = up, z = -forward.
  * Rows are `[2n/(r-l),0,(r+l)/(r-l),0] [0,2n/(t-b),(t+b)/(t-b),0] [0,0,n/(f-n),nf/(f-n)] [0,0,-1,0]`. This is
    **reversed Z**: near maps to 1, far maps to 0.
  * cvars: `r_znear` default "0.01" (cvar 0x143BDA980), `r_zfar` default "7000" (cvar 0x143BDAA00).
* LOD: `0x140268680` picks each render entity's LOD from **min(dist to vieworg, dist to m_portalViewOrg)** of the main
  view **[V]**. That is why the Timepiece writes the portal origin into the player view's m_portalViewOrg.
* Visibility for the portal view is computed by the renderer from the portal camera **[V]**:
  * `0x1403AE660` sets up the Umbra camera with position = cameraPos + m_umbraOffsetWs (+0x3B14). Portal views use
    m_subrect_projectionMatrix.
  * If all of customClipPlane != FLT_MAX, it calls `0x140C41D40(umbraCam, &plane, 1)`, i.e. Umbra
    `setUserClipPlanes`.
  * `0x1403AFB40` frustum-tests each render entity and writes `m_umbraVisibilityInPortal` (render entity +0x1C31 bit 3)
    instead of `m_umbraVisibility` (bit 2) when `+0x3A34 & 1` (the portal flag above) is set.

---

## 2. The render target `_TimeTravelPortal`

* Created in `0x14050FFF0` (device init) at 0x14051012A through **`0x1403751E0` idImageManager::ScratchImage(_, name, opts, 0, 1)**
  **[V]**. If an image with that name already exists it is reused (lookup `0x140085F20`). Stored in `this+0x68`.
* `opts` (stack, 0x4C bytes) **[V]**. Byte offsets:
  * +0x00 = 0
  * +0x04 = format bits `0x5CB | (FUN_140341570((f>>6)&0xF, (f>>10)&7, &x) ? x<<13 : 0)`
  * +0x08/+0x0C, +0x10/+0x14, +0x18/+0x1C = width/height from `rs vt[0x218]` / `vt[0x220]` (full render resolution)
  * +0x20 = 0
  * +0x24..+0x38 = 1 (x6)
  * +0x3C = `[0x1424FF60C]`
  * +0x40 = byte 0
  * +0x44 = 0
  * +0x48 = 0x30040

  Bits 21+ of the format word come from an uninitialized stack slot in the stock code. Use 0 for them.
* Resolution: the full render width x height. The backend re-creates the image if the output size changes **[V]**.
* Render target binding: the backend uses the image as the output of `RenderSingleView` and limits the work to the
  scissor rect. Pixels outside the rect are stale, but they are never sampled, because the lens only covers the rect.
* The material binding goes through the owner's render entity: `re = entity+0xF0`, parm block `re+0x1950`
  (`gameParmBlock`, reflection 0x141763A90), parm decl `rendering/portal` (handle `*(void**)0x14254E368`), value
  `{RT+0x98, 0}` **[V]**.

---

## 3. How the lens displays it (material and shader) **[V]**

* `cpntTimeShiftDevice` decl: `m_materialToReplace = models/weapons/timeshift_device/timeshift_device_alpha.material`
  and `m_portalMaterial = models/effects/materials/gameplay/powers/timetravel/portal.material`. The swap is done per
  surface: init builds two idLists of per-surface materials, then calls `renderEntity->vt[0x60](re, &list)` **[V]**.
  Any render entity's surfaces can be remapped this way.
* `portal.material`: `cull back`, options `hasBumpMap 1, hasGlossMap 1, hasMetallicMap 1, useSkinning 1`,
  `mainProgram rendering/timeTravelPortal`. Parms: `tsd/RefractionIOR 0.475`, `tsd/RefractionStrength {15,25}`,
  `tsd/BlurStrength 22`, `tsd/GlassMaskMap ..._portalmask.tga`, `tsd/failurenoisemap`,
  `tsd/FailureRefractionStrength`, `tsd/FailureBlurStrength`. The `tsd/FailFactor` parm comes from code.
* Shader `renderprog/rendering/timetravelportal.decl` (opaque, lit glass):
  ```hlsl
  float2 uv = screenPosToTexcoord($sv_position.xy + (refractionOffset + failureRefraction)*$resolutionScale.xy,
                                  $positionToViewTexture, $resolutionScale.xy);
  ... Poisson blur: $(rendering/portal).SampleLevel($linearClamp, (uv/$resolutionScale.xy + offset)
                                                   * $(rendering/portalResolutionScale).xy, 0)
  color = lit*lerp(masks.x, ...) + portalView * portalFactor * (1-masks.x)   // masks = GlassMaskMap.xy (x=glass, y=blur)
  ```
  The shader samples in **screen space**. The mesh UVs only drive the masks and the noise. With `RefractionStrength {0,0}`,
  `BlurStrength 0`, FailFactor 0 and a black GlassMaskMap, the surface shows the RT 1:1.
* `rendering/portal` and `rendering/portalResolutionScale` are renderparms (`{Texture2D float4 _default}`,
  `{Uniform float2 {1,1}}`).
* A full scan of all stock decls (catalogs 1-4) found that only `timetravelportal.decl` samples `rendering/portal`, and
  only `portal.material` uses that shader **[V]**.
* Shader cache: `base/shaderCache/ShaderCache_PC.*` contains the option names `useskinning`, `rendering/portal` and
  `rendering/portalresolutionscale`. Which permutations exist is **unknown** (the .uci/.ucd index was not decoded).
  Only the device lens (skinned md6 mesh) is known to draw with this material, so the **skinned** permutation is
  certain and a static-mesh permutation is not **[?]**. Earlier experience (use_softcontact) shows that a missing
  permutation freezes the renderer.

---

## 4. Clip plane / oblique near plane / other subviews

* **customClipPlane is used only by Umbra** **[V]**: `0x1403AE660`:
  ```c
  for (i=0;i<4;i++) ok &= view->m_gameRV.customClipPlane[i] != FLT_MAX;
  if (ok) Umbra_SetUserClipPlanes(&umbraCam, &view->customClipPlane, 1);   // 0x140C41D40 (copies to cam+0x40, count at +0x14C)
  ```
  No consumer in the projection or shaders was found. There is no clip-plane builtin among the shader parm names
  (0x141E5AA40..), and no reads of `m_renderRV+0xF0` (0x3AC0) in the renderer **[V]**. So the plane improves
  **occlusion culling** for a camera behind a wall, but it does **not** hide geometry between the camera and the exit
  surface.
* **Explicit projection** is supported **[V]** (`0x140418970`: `if (rv->useExplicitProjectionMatrix) proj = rv->explicitProjectionMatrix`).
  The portal path zeroes the flag at `0x140409D2F` (`C6 80 40 01 00 00 00` = `mov byte [rax+0x140],0`). If those 7 bytes
  are NOPed, an oblique near plane (Lengyel) for reversed Z works like this:
  * Near plane = row3 - row2 and far plane = row2 (clip space 0 <= z <= w).
  * Clip plane in view space: `C = (R*n, n.org + d)`. Here `R` rows are (-left, up, -forward) of the portal camera axis
    and `n.p + d >= 0` is the side beyond the exit gate.
  * Replace `row2' = row3 - a*C`, with `a = (row3.Q)/(C.Q)` and `Q = P^-1 * (sgn(Cx), sgn(Cy), 0, 1)` (the far corner).
    The x/y rows stay unchanged, so screen-space alignment with the main view is preserved.

  Verify the signs in game.
* Mirrors: the renderer tracks `m_viewMatrixHasNegativeDeterminant` (idRenderView+0x77B0), so a mirrored axis is
  handled for culling **[V]**. Nothing in the stock game uses it.
* Other "views" found:
  * `cpntRenderView` (killcam/freecam/midnight/menu cams) is a full-screen camera override that owns an idView.
  * `m_frameCapture` / `_frameCapture` is a frame copy.
  * The key_hole_peek / UI iggy rendertargets are Iggy only.

  None of these is render-to-texture of the world **[V]**.

---

## 5. Hooking plan for Void Gate

All engine calls must run on the game thread, in the same frame window as the Timepiece's phase. That window is after
the player camera update and before the main-view submit `0x140A393A0`. The render-frame call
`rs->vt[0xB8]` = 0x1403FDEB0 -> 0x1403FF930 -> 0x1403FF3E0 consumes the slot.

### 5.1 Per-frame hook point (needed for a, b) — risk LOW
Detour **`0x140A393A0`** (main view submit; `(idGameLocal* G, frame*)`; sets `[0x142710D68] = G`). Its prologue is clean,
15 bytes: `40 53 55 56 57 41 55 41 56 41 57 48 83 EC 40`. Call sites are 0x140A35E52, 0x140A390C8 and 0x140A39231
(three callers), so patching the entry is simpler.
* **pre**: compute the active gate and the virtual camera. Set the player view's `m_portalViewOrg` (`owner+0xF0`) to the
  virtual origin so LODs near the exit are high.
* **post**: the player idView is now finalized (`0x140A10780` already ran inside). Build view[1] (5.2) and arm the
  portal (5.3).
Alternative: put the logic into the existing component-tick hooks. The camera may then be one frame old, which shows as
visible swim at the gate edge when turning. Not recommended.

### 5.2 Fill view[1] with an arbitrary camera (b) — risk LOW-MEDIUM
```text
rw = G->vt[0x48](G);  n = rw->vt[0xB0](rw);  if (n < 2) skip;  v1 = rw->vt[0xC0](rw, n-1);
0x140064A90(v1, &ownerPlayer->view /*owner+0x30*/);     // copy the finalized renderView_t (env, fog, post parms, settings)
v1->vieworg(+0xA8) = camOrg;  v1->m_portalViewOrg(+0xC0) = camOrg;  v1->previousvieworg(+0xB4) = last camOrg (or camOrg)
v1->viewaxis(+0xCC) = camAxis;                           // virtual cam = T_B * Rz(180) * T_A^-1 * player cam
// optional with 5.5: v1->explicitProjectionMatrix(+0x100) = oblique(P_main, C); v1->useExplicitProjectionMatrix(+0x140) = 1
```
* Gate transform: entity origin +0x300 and axis +0x30C (3x3).
  `camAxis = R_B * Rz180 * R_A^T * playerAxis` and `camOrg = P_B + R_B*Rz180*R_A^T*(playerOrg - P_A)`.
* Keep the copied settings+4 (+0x50, time space). In normal levels it is 0; the Timepiece writes 0 or 2.
* The env parms are the player's, so the portal will not pick up the exit area's fog/LUT. A faithful version would own
  an `idView` (0x250F0 bytes, ctor 0x140A0EF60, never freed) and run `0x140A139F0` + `0x140A10FF0` + `0x140A10780` on it
  like the update/render phases do. That costs more and carries MEDIUM risk.
* After a gate teleport, set `m_justTeleported` (+0xBCFC) = 1 on all render world views, as `0x140512400` does, to drop
  temporal history.

### 5.3 Arm the portal render every frame without the Timepiece (a) — risk LOW-MEDIUM
```text
rect6 = scissor of the gate outline (8 points of the oval's box) projected with the main camera;
        clamp to v0 = rw->vt[0xC0](rw,0) m_unscaledViewport (+0x7878); if any point is behind the camera -> full viewport;
        need > 16 px like the stock code; {x1,y1,x2,y2,0,0} in pixels, y from top
plane4 = exit plane (n_B, -n_B.P_B) in world space, sign to verify (FLT_MAX x4 = off). Adjust it if m_umbraOffsetWs != 0
rs = (void*)0x14359FB20;  (*(void(**)(...))(*(u64*)rs + 0x1B8))(rs, ourRT, v1, rect6, plane4);
```
* Our RT: `ScratchImage("_VoidGatePortal", &opts, 0, 1)` (`0x1403751E0`, opts as in section 2), created once on the game
  thread. A second name is needed only for a second pass (5.6).
* Re-arm every frame, because the backend clears `rs+0x88` after use.
* On gate close, call `rs->vt[0x1C0](rs)` and restore nothing else.
* Conflict: in mission 7 the Timepiece arms the same slot earlier in the frame. Our post-hook wins (last writer). Do not
  arm when the Timepiece is unfolded.
* Thread safety matches the stock path: the game thread writes `v1->m_gameRV` and arms the slot, and the backend reads
  them during the frame's render call. **[I]**: the exact thread handoff was not traced.

### 5.4 Show the texture on the gate (c) — risk MEDIUM-HIGH (shader permutation)
* Needs a **render entity with a mesh** (not FX particles). Bind the parm every frame like 0x140511E42..E6C:
  `0x1403DC040(re+0x1950, h)` then `0x1403DBCF0(re+0x1950, h, &{RT+0x98,0})`, with `h = *(void**)0x14254E368` and
  `re = gateEntity+0xF0`.
* Material: a copy of `portal.material` that changes only parms and textures. Do not touch the options:
  * `tsd/GlassMaskMap` -> a black texture (portal everywhere; check that `_black` exists)
  * `tsd/RefractionStrength {0,0}`
  * `tsd/BlurStrength 0`
  * `tsd/FailureRefractionStrength {0,0}`, `tsd/FailureBlurStrength 0`
  * no failure noise effect (FailFactor parm unset = default)

  Set it through the def or at runtime through `re->vt[0x60](re, &perSurfaceMaterialList)` (the device's method).
* Mesh: the material is `useSkinning 1` and opaque, with no alpha. The only skinned flat plane in the game is
  **Delilah's portal plane** `models/effects/map_specific/dunwall_return_tower/portal.md6` (catalog 2; 5.87 x 3.63 m,
  4 mm thick; stock material `painting_temp_01.material`; inherits `midnight_animated_parent.def`). Options:
  1. Use that plane with the material remapped. It shows a rectangle, so cover the edges with the existing oval ring FX.
  2. Make a copy of its `.bmd6model` with the vertices reshaped into a 1.7 x 2.8 oval, keeping the vertex format and
     counts (needs a bmd6 writer).
  3. Use a static oval mesh. **Unknown** whether the non-skinned `timeTravelPortal` permutation is cached, which is a
     freeze risk. Test only with the watchdog.

  Remapping the material on a skinned md6 should hit the same permutation as the device lens **[I]**, provided both md6
  vertex layouts are the same.
* Each gate entity can bind its own image, because the binding is per render entity.

### 5.5 Clip geometry behind the exit gate — patch `0x140409D2F` (7 x NOP) — risk MEDIUM (visual), LOW (stability)
* This keeps `useExplicitProjectionMatrix` for the portal view. Supply the oblique matrix (section 4) in v1.
  * Stock behavior is unchanged: the Timepiece's view has the flag 0.
  * Side effects: no TAA jitter on the portal view, and possible artifacts in depth-based effects (HBAO+ via
    `GFSDK_SSAO_D3D11.dll`, fog, SSR, DOF) because the depth mapping is tilted.
* Without this patch, the wall behind gate B and everything behind it will cover the portal image whenever the player is
  more than a wall-thickness away. The Umbra clip plane alone does not help with that.

### 5.6 Two live gates per frame — risk HIGH
The engine has 1 render world portal view, 1 rs slot and 1 portal visibility bit per render entity. Options:
* **Recommended first**: per frame, render only the gate with the larger on-screen rect. The other gate shows the
  fallback FX. Stale RT content cannot be reused because sampling is screen-space.
* Alternate frames when both are visible (30 Hz each, slight misregistration on fast turns).
* True second pass: hook the call at 0x1403FF73F (`call [rax+0x1C8]`) and call `0x140409AA0` twice. Re-arm `rs+0x88..+0xBC`
  between the calls, with a 3rd idRenderView (`0x140419140(alloc 0xBD00, rw, 2)`) and a 2nd RT. Unverified: per-view
  resources sized for 2, culling order (visibility of the 2nd pass) and occlusion queries.

### 5.7 Risk table
| Step | What | Risk |
|---|---|---|
| 5.1 | detour 0x140A393A0 (pre/post) | low |
| 5.2 | write view[1] renderView_t by copy + override org/axis | low-medium |
| 5.3 | ScratchImage + rs vt[0x1B8] each frame, vt[0x1C0] on close | low-medium |
| 5.3 | customClipPlane for Umbra (sign/offset) | low (worst case wrong culling) |
| 5.4 | mesh + portal material copy + parm binding | medium-high (shader cache freeze if wrong permutation) |
| 5.5 | NOP 0x140409D2F + oblique projection | medium visual / low crash |
| 5.6 | second portal pass | high |
| perf | every armed frame is a full second scene render (culling, lighting, post) inside the scissor | the Timepiece pays the same cost |

---

## 6. Open questions
1. Do the screen-space RT and the lens pass in the main view double-tonemap? The portal pass sets several renderSettings
   bits (+0x74..+0x84) whose meaning is unknown. The stock lens looks right, so copying the Timepiece settings path
   should be fine.
2. Umbra user-plane sign and space (world space? `m_umbraOffsetWs`?). Test both signs.
3. Which shader permutations of `rendering/timeTravelPortal` exist: static vs skinned, and md6 layout equality between
   the device and Delilah's plane.
4. Exact thread and handoff of `rs vt[0xB8]` (render frame) relative to the game update. Assumed to be the same
   window as the Timepiece phase.
5. Recursion (seeing gate B inside gate A) is not supported. The B mesh will show stale or `_default` texture in the A
   view; consider hiding the other gate's mesh in the portal pass (unknown how).
6. The setter of `m_portalMaterialSet` (bit 2) in the unfold player state was not traced. It is not needed for the mod.

## 7. Address index
| Address | What |
|---|---|
| 0x14050FC40 / 0x14050FFF0 | TSD ctor / init (RT `_TimeTravelPortal`, lens joints) |
| 0x140510A30 / 0x140510DE0 | TSD activate / deactivate (vt5 / vt6; deactivate calls rs vt[0x1C0]) |
| 0x140510FC0 | timeline offset (org +/- `*(*(*(playerCpnt+0x28)+0x230)+0xB0)`) |
| 0x140511210 / 0x140511580 | TSD update / **portal render** (factory 0x141F44088 slots 4 / 9 via 0x140B01A10 / 0x140B01A70) |
| 0x140512400 | the actual time shift (teleport, sets m_justTeleported on all views) |
| 0x140A0EF60 | idView ctor; 0x140A0F8D0 SetOrgAxis; 0x140A10780 Finalize; 0x140A13240 copy cam offsets; 0x140A13450 game VP |
| 0x140A393A0 | main view submit (player idView -> frame), called at 0x140A35E52 / 0x140A390C8 / 0x140A39231 |
| 0x140064A90 | renderView_t::operator= |
| 0x1402A12A0 / 0x1402A2D70 / 0x1402A1260 | render world ctor (2 views) / GetView / NumViews |
| 0x140409A10 / 0x140409A90 / 0x140409AA0 | rs vt[0x1B8] SetPortalView / vt[0x1C0] Clear / vt[0x1C8] RenderPortalView |
| 0x140409D2F | `mov byte [rax+0x140],0` (explicit projection off) - patch point |
| 0x140419A20 / 0x140419D00 / 0x140418970 | idRenderView viewport+scissor / setup / projection (explicit or fov) |
| 0x1401552C0 / 0x140154F80 | frustum projection (reversed Z) / view matrix |
| 0x1403AE660 / 0x140C41D40 | Umbra camera setup / setUserClipPlanes (customClipPlane consumer) |
| 0x1403AFB40 | per-entity visibility (m_umbraVisibility / ...InPortal, re+0x1C31 bits 2/3) |
| 0x140268680 | LOD distance uses min(vieworg, m_portalViewOrg) |
| 0x1403FF3E0 (call at 0x1403FF73F) | render frame: portal first, then main views |
| 0x1403751E0 | idImageManager::ScratchImage(name, opts, 0, 1) |
| 0x1403DC040 / 0x1403DBCF0 | idParmBlock::Remove / Set (re+0x1950, parmDecl, &value16) |
| 0x14254E368 / 0x14254D7C8 / 0x14254E388 | parm decl handles: rendering/portal, rendering/portalResolutionScale, tsd/FailFactor |
| 0x14254DC78 | float2 value pushed to rendering/portalResolutionScale (= main m_resolutionScale) |
| 0x143BE0988 | `ark_timeshift_skipportal` value (only used in update) |
