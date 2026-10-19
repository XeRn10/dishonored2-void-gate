// Void Gate (Dishonored 2): a Portal-style power for Emily and Corvo -- gameplay module (voidgate.dll).
//
// The power itself is data (d2mods/VoidGate): an inventory item whose equipment has a Far Reach (arkCpntPowerGrapple)
// component with m_powerId = ARK_PLAYER_POWER_TIMETRAVEL (8). The host (dinput8.dll, host.c) hooks the grapple cast
// and the window message loop and forwards them here; this module is reloaded whenever voidgate.dll changes.
//   * cast: for power 8, the aimed surface point becomes the next gate (A, B, A, ...) instead of pulling the player;
//     mana and cooldown go through the stock base cast.
//   * update (right after the Void Gate component's own per-frame update, on whatever game job thread runs it --
//     the same context in which Far Reach plays its FX): gate FX, and anything touching one gate comes out of the
//     other, through the engine's deferred "setviewpos" event (player, NPCs, loose objects).
//   * tick (window message thread): never touches the game; it only reports when updates stop.
// All addresses are for Dishonored2.exe 1.77.9.0 (GOG, PE timestamp 0x5C050611, no ASLR); see docs/d2_void_portal.md.
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "vg_portal.h"

static const vg_host *g_host;
#define vg_log g_host->log

#define G_GAME 0x142710D68ull               // idGameLocal* (global holds the pointer)
#define OFF_ENTITIES 0x312FF8               // game + : idEntity *entities[0x4000], index = handle >> 16; [0] = player
#define MAX_ENTITIES 0x4000
#define OFF_VIEWS 0x1F52B8                  // game + : list of render view holders (last one = player view)
#define OFF_VIEWS_N 0x1F52C0
#define OFF_EVQ 0x1F5250                    // game + : game event queue (setviewpos posts here)
#define OFF_EVQ_PENDING 0x1F5278
#define EV_SETVIEWPOS 0x1800C2u             // payload: entity id, origin (feet), pitch, yaw, roll
#define GAME_VT_FXSYS 0x388                 // game vtable: -> FX system

#define ENT_ID 0xA8                         // entity: u32 handle
#define ENT_ORIGIN 0x300                    // entity: idVec3 origin (feet)
#define ENT_AXIS 0x30C                      // entity: idMat3 axis (row 0 = forward)

// class-number ranges {min, max} of entity type infos (entity->vtable[0]() + 0x60), as Far Reach uses them to pick
// its target FX
#define CLASS_NPC 0x143C21610ull
#define CLASS_MOVABLE_A 0x143C1D0E0ull
#define CLASS_MOVABLE_B 0x143C1C7E0ull

#define FN_POWER_BASECAST 0x140737830ull    // arkCpntPower cast: mana, cooldown, usage count
#define FN_EVQ_ALLOC 0x14013E790ull         // (queue, size) -> record
#define FN_TELEPORT 0x1409FD300ull          // idEntity::Teleport_r(ent, origin*, axis*, char, recursion)
#define FN_FX_START 0x1409FC440ull          // (ownerEnt, u32 *handle, -, -, fxDecl, pos*, axis*, 0, 0, 0)
#define FN_FX_STOP 0x1409FC600ull           // (-, -, -, handle)
#define FN_FX_MOVE 0x1409FC6B0ull           // (ownerEnt, handle, pos*, axis*)
#define FN_FX_FIND 0x1401FFA80ull           // (fxSystem, handle) -> live FX or 0
#define FN_DIR_TO_AXIS 0x14015CFF0ull       // (dir*, idMat3 *out) -> out, row 0 = dir
#define FN_SPAWN 0x140A3A4A0ull             // (game, entityDef, origin*, axis*, owner, char, 0, 0, 0) -> entity
#define FN_DECL_LOAD 0x14001A7E0ull         // (decl, force, idStr *error): re-parse from source text (editor only)
#define FN_ALLOCATOR 0x1401B0D50ull         // -> allocator; vt+0xA8 = tag for idStr
#define DECL_NAME 0x08                      // decl: const char *name
// idDeclFX layout (FX playback code 0x140288550): groups idList at +0x58 (count +0x60), group stride 0x130,
// group particle systems idList at +0x30 (count +0x38, stride 0xAE0), particle system m_decls idList at +0x18
#define FXDECL_GROUPS 0x58
#define FXGROUP_SIZE 0x130
#define FXGROUP_PARTICLES 0x30
#define FXPARTICLE_SIZE 0xAE0
#define FXPARTICLE_DECLS 0x18

#define CPNT_DECL 0x08
#define DECL_POWER_ID 0x190
#define POWER_VOIDGATE 8
// arkDeclCpntPowerGrapple FX fields (reflection). The Void Gate decl reuses two the power never plays:
// m_bloodFlyNestTargetFX = the gate (voidgate_gate_01.fx), m_pickupImpactFX = the Timepiece transition.
#define DECL_FX_WORLD_TARGET 0x1B8          // stock aim marker, fallback gate FX
#define DECL_FX_GATE 0x1E8
#define DECL_FX_PASS 0x228                  // m_worldImpactFX: burst at both gates
#define DECL_FX_TRANSIT 0x260
// m_dummyEntityDef: Far Reach's pull helper; ours is the gate entity (voidgate_gate.def = idEntity + cpntFX playing
// voidgate_gate_01.fx). A spawned entity gets its resources loaded and its FX run the way the engine expects.
#define DECL_GATE_DEF 0x320
// arkCpntPowerGrapple aim data (found from cast dumps, see docs)
#define GRAPPLE_TARGET_VALID 0xD7           // u8
#define GRAPPLE_TARGET_ENT 0xDC             // u32 handles: NPC target, object target (0x3FFF0010 = none)
#define GRAPPLE_AIM_FX 0xC0                 // FX decl of the aim marker now shown (0x1405047A0)
#define GRAPPLE_AIM_FX_HANDLE 0xC8          // its FX handle (-1 = none)
#define GRAPPLE_HIT_NORMAL 0xFC             // idVec3 surface normal of the aimed point
#define GRAPPLE_HIT_POS 0x108               // idVec3 aimed surface point
#define GRAPPLE_STAND_POS 0x120             // idVec3 where Far Reach would put the player

#define BODY_HEIGHT 1.8f
#define BODY_RADIUS 0.4f

typedef void (*cpnt_fn)(uintptr_t comp);
typedef uintptr_t (*evq_alloc_fn)(uintptr_t queue, int size);
typedef void (*teleport_fn)(uintptr_t ent, const float *origin, const float *axis, char, int);
typedef uint32_t *(*fx_start_fn)(uintptr_t ent, uint32_t *handle, uintptr_t, uintptr_t, uintptr_t fxDecl,
                                 const float *pos, const float *axis, uintptr_t, uint32_t, uintptr_t);
typedef void (*fx_stop_fn)(uintptr_t, uintptr_t, uintptr_t, uint32_t handle);
typedef void (*fx_move_fn)(uintptr_t ent, uint32_t handle, const float *pos, const float *axis);
typedef uintptr_t (*fx_find_fn)(uintptr_t fxSystem, uint32_t handle);
typedef float *(*dir_to_axis_fn)(const float *dir, float *axis);
typedef uintptr_t (*vfn0)(uintptr_t self);

typedef struct {
    int valid;
    float pos[3];   // surface point (gate center)
    float n[3];     // surface normal, pointing out of the surface
    float ground;   // player feet height when the gate was cast (floor hint for wall gates)
    uint32_t fx;    // idle FX handle (0 = none) -- unused: the gate entity plays the FX
    DWORD fxRetryAt, fxCheckAt;
    uint32_t ent;   // gate entity handle (0 = not spawned yet)
    int dirty;      // moved by a cast: the entity must follow
    uintptr_t fxComp;  // the gate entity's arkCpntFX (seen in its update), to stop its looping FX before removal
    float up[3];    // the gate's up in its plane (walls: world up; floors/ceilings: the caster's facing)
} gate_t;

typedef struct {
    uint32_t id;    // entity handle that just came through; ignored until it stops touching both gates
    DWORD since;
} passed_t;

#define MAX_PASSED 32
#define MAX_BURSTS 8
#define MAX_FLASHES 16

#define STATE_MAGIC 0x56470012u  // bump when vg_state changes layout
typedef struct {
    uint32_t magic;
    gate_t gate[2];
    int next;                 // which gate the next cast places
    uintptr_t lastPlayer;     // reset gates when the player entity changes (new map / load)
    DWORD noTeleportUntil;
    int mustLeave;            // after a teleport, wait until the player is out of both gates
    DWORD verifyAt;           // log where the player really ended up after a teleport
    float verifyTo[3];
    DWORD castThread;
    uintptr_t fxGate, fxPass, fxTransit;  // FX decls taken from the Void Gate component decl at cast time
    uintptr_t fxFallback, fxGateSrc, fxGateChecked, fxTransitChecked;
    uintptr_t gateDef;        // entityDef of the gate entity
    passed_t passed[MAX_PASSED];
    struct { uint32_t fx; DWORD until; } burst[MAX_BURSTS];
    int npcDirect;            // NPCs: 0 = setviewpos event (deferred), 1 = idEntity::Teleport_r right away
    int exitGate;             // gate the player just came out of: ignored until the player steps off it (-1 none)
    float rescueTo[3];        // where the player stood before the last teleport (fell through the world -> back)
    float rescueYaw;
    DWORD rescueCheckAt;
    DWORD lastTick;
    uint32_t loggedIds[16];   // entities already reported as touching a gate (diagnostics)
    int wallPress[2];         // frames the player has been pressing into each wall gate
    float pendingVz;          // exit speed to apply once the teleport has happened
    DWORD pendingVzUntil;
    int pendingVzFrames;
    struct { uint32_t id; DWORD at; float to[3]; float yaw; } npcCheck[4];
    int stuckGate, lastFrom;  // gate whose teleport the engine refused (-1 none); gate last gone into
    int safeLanding;          // fall protection: came out of a gate, until the player has landed
    DWORD safeUntil;          // fall protection: dropping through the column above a floor gate
    float vzEst, lastZ;       // measured vertical speed
    struct { uint32_t id; DWORD at; } flash[MAX_FLASHES];  // pass flash entities to remove
    LONGLONG lastZTime;
    DWORD updates, updatesSince, updateThread;
    DWORD safeLandingSince, stillSince;
    float zeroedMax;          // largest fall height zeroed during this protection (log)
    float vEst[3], lastPos[3];  // measured velocity (origin deltas, smoothed)
    float pendingV[3];        // exit velocity to apply once the teleport has happened
    int pendingVOn;
} vg_state;

static vg_state *S;
static void safe_landing_start(void);
static void window_update(void);
#define WIN_PIECES 10  // a window: the stock rectangle drawn as this many parallelograms, a 2*N-gon in the oval
typedef struct {
    uint32_t ids[WIN_PIECES];  // its pieces (entities)
    uint32_t gateEnt;          // the gate entity it was put on (a recast replaces it: the window follows)
} window_t;
static window_t g_win[2];  // the see-through window on gate w (showing the other gate's side)
static int is_window_id(uint32_t id)
{
    for (int w = 0; id && w < 2; w++)
        for (int k = 0; k < WIN_PIECES; k++)
            if (id == g_win[w].ids[k])
                return 1;
    return 0;
}
// ---------------------------------------------------------------- upgrades (bought in the journal)
// The power's three upgrades are inventory items (playerpowersystem m_powerUpgradeItemList: BLINK_BLINKATTACK ->
// voidgate_far, FARREACH_PULL_ATTACK -> voidgate_through, MESMERIZE_ANIMAL -> voidgate_momentum = "Crossfire").
// Owned = quantity > 0 in the player's arkCpntInventory, read as the journal does (0x14051C010): entry index from
// 0x14072DE00(inventory, itemDecl), entries at inventory+0x38, 0x38 bytes each, quantity int at +0x20.
#define TYPE_INVENTORY 0x143C2D850ull  // GetComponent type the game passes before its inventory lookups
#define FN_INV_INDEX 0x14072DE00ull    // (inventory, itemDecl) -> entry index, -1 = none
enum { UPG_FAR, UPG_THROUGH, UPG_CROSSFIRE, UPG_COUNT };
static const char *const g_upgItem[UPG_COUNT] = {
    "components/characters/player/base/attributes/voidgate_far.inventoryitem",
    "components/characters/player/base/attributes/voidgate_through.inventoryitem",
    "components/characters/player/base/attributes/voidgate_momentum.inventoryitem"};
static int g_upg[UPG_COUNT];
static int g_upgForce = -1;  // test channel "upg <mask>": -1 = read the inventory
static int item_quantity(uintptr_t p, const char *name);
static void upgrades_update(void);
#define HAS_UPG(u) (g_upgForce >= 0 ? (g_upgForce >> (u)) & 1 : g_upg[u])
static int g_hudPrompt = 1;  // test channel "hudprompt 0|1" (screenshots without our prompt)
static int g_surfCheck = 0;  // solid-surface check for gates ("surfcheck"): off until gates shift to fit (too strict on real walls)
static const char *surface_problem(const float P[3], const float nIn[3]);

// ---------------------------------------------------------------- memory helpers
static int readable(uintptr_t p, size_t n)
{
    if (p < 0x10000 || p > 0x7FFFFFFFFFFFull)
        return 0;
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery((void *)p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        return 0;
    return p + n <= (uintptr_t)m.BaseAddress + m.RegionSize;
}

static uintptr_t game(void)
{
    uintptr_t g = *(uintptr_t *)G_GAME;
    return readable(g + OFF_ENTITIES, 8) ? g : 0;
}

static uintptr_t player(void)
{
    uintptr_t g = game();
    if (!g)
        return 0;
    uintptr_t p = *(uintptr_t *)(g + OFF_ENTITIES);
    return readable(p + ENT_ORIGIN, 12) ? p : 0;
}

static uintptr_t fx_system(void)
{
    uintptr_t g = game();
    return g ? ((vfn0)(*(uintptr_t *)(*(uintptr_t *)g + GAME_VT_FXSYS)))(g) : 0;
}

// Player view: eye position and forward vector from the last render view holder (same as "getviewpos").
static int player_view(float eye[3], float fwd[3])
{
    uintptr_t g = game();
    if (!g)
        return 0;
    uint32_t n = *(uint32_t *)(g + OFF_VIEWS_N) & 0xFFFFFF;
    uintptr_t list = *(uintptr_t *)(g + OFF_VIEWS);
    if (!n || !readable(list + (n - 1) * 8, 8))
        return 0;
    uintptr_t v = *(uintptr_t *)(list + (n - 1) * 8);
    if (!v || !readable(v - 0x20 + 0xD8, 0x48))
        return 0;
    v -= 0x20;
    memcpy(eye, (void *)(v + 0xD8), 12);
    memcpy(fwd, (void *)(v + 0xFC), 12);  // first row of the view axis = forward
    return 1;
}

static int entity_class(uintptr_t e)
{
    uintptr_t vt = *(uintptr_t *)e;
    if (vt < 0x140000000ull || vt > 0x143000000ull)
        return -1;
    uintptr_t ti = ((vfn0)(*(uintptr_t *)vt))(e);
    return readable(ti + 0x60, 4) ? *(int *)(ti + 0x60) : -1;
}

static int class_in(int c, uintptr_t range) { return c >= ((int *)range)[0] && c <= ((int *)range)[1]; }

static float dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static float yaw_of(const float v[3]) { return atan2f(v[1], v[0]) * 57.2957795f; }
static float dist2(const float a[3], const float b[3])
{
    float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return dot3(d, d);
}

static int gate_kind(const gate_t *g)  // 1 floor, -1 ceiling, 0 wall
{
    return g->n[2] > 0.7f ? 1 : g->n[2] < -0.7f ? -1 : 0;
}

// horizontal unit normal of a wall gate
static void wall_normal(const gate_t *g, float h[3])
{
    float l = sqrtf(g->n[0] * g->n[0] + g->n[1] * g->n[1]);
    h[0] = l > 1e-3f ? g->n[0] / l : 1, h[1] = l > 1e-3f ? g->n[1] / l : 0, h[2] = 0;
}

// ---------------------------------------------------------------- FX
static uint32_t fx_start(uintptr_t fxDecl, const float pos[3], const float dir[3])
{
    uintptr_t p = player();
    if (!p || !fxDecl)
        return 0;
    float axis[9];
    ((dir_to_axis_fn)FN_DIR_TO_AXIS)(dir, axis);
    uint32_t h = 0xFFFFFFFF;
    ((fx_start_fn)FN_FX_START)(p, &h, 0, 0, fxDecl, pos, axis, 0, 0, 0);
    return h == 0xFFFFFFFF ? 0 : h;
}

static int fx_alive(uint32_t h)
{
    uintptr_t fs = fx_system();
    return h && fs && ((fx_find_fn)FN_FX_FIND)(fs, h) != 0;
}

static void fx_stop(uint32_t h)
{
    if (fx_alive(h))
        ((fx_stop_fn)FN_FX_STOP)(0, 0, 0, h);
}

// Load a decl now (our decls are new resources, so no map precaches them) and log its parse error, if any.
typedef struct {
    char *data;
    int len;
    uint32_t alloced;
    char buf[24];
} id_str;

static int decl_load(uintptr_t decl)
{
    if (!readable(decl, 0x70))
        return 0;
    uintptr_t a = ((vfn0)FN_ALLOCATOR)(0);
    id_str err = {0};
    err.data = err.buf;
    err.len = (int)(((int (*)(uintptr_t))(*(uintptr_t *)(*(uintptr_t *)a + 0xA8)))(a) << 0x1D);
    err.alloced = sizeof err.buf;
    int r = ((int (*)(uintptr_t, int, id_str *))FN_DECL_LOAD)(decl, 1, &err);
    const char *name = readable(*(uintptr_t *)(decl + DECL_NAME), 1) ? *(const char **)(decl + DECL_NAME) : "?";
    if (r)
        vg_log("decl %s: load error %d: %s", name, r, err.data && readable((uintptr_t)err.data, 1) ? err.data : "?");
    return r == 0;
}

// Load an FX decl and the particle decls it plays; 1 if its layout looks sane afterwards.
static int fx_prepare(uintptr_t fx)
{
    if (!fx || !decl_load(fx))
        return 0;
    uintptr_t groups = *(uintptr_t *)(fx + FXDECL_GROUPS);
    uint32_t ng = *(uint32_t *)(fx + FXDECL_GROUPS + 8) & 0xFFFFFF;
    vg_log("FX %s: %u group(s) at %p", *(const char **)(fx + DECL_NAME), ng, (void *)groups);
    if (!ng || ng > 16 || !readable(groups, ng * FXGROUP_SIZE))
        return 0;
    for (uint32_t g = 0; g < ng; g++) {
        uintptr_t grp = groups + g * FXGROUP_SIZE;
        uintptr_t ps = *(uintptr_t *)(grp + FXGROUP_PARTICLES);
        uint32_t nps = *(uint32_t *)(grp + FXGROUP_PARTICLES + 8) & 0xFFFFFF;
        if (nps && (nps > 64 || !readable(ps, nps * FXPARTICLE_SIZE)))
            return 0;
        for (uint32_t i = 0; i < nps; i++) {
            uintptr_t list = *(uintptr_t *)(ps + i * FXPARTICLE_SIZE + FXPARTICLE_DECLS);
            uint32_t n = *(uint32_t *)(ps + i * FXPARTICLE_SIZE + FXPARTICLE_DECLS + 8) & 0xFFFFFF;
            for (uint32_t k = 0; k < n && k < 16 && readable(list, n * 8); k++) {
                uintptr_t pd = *(uintptr_t *)(list + k * 8);
                int ok = decl_load(pd);
                vg_log("  particles %s: %s", readable(pd + DECL_NAME, 8) ? *(const char **)(pd + DECL_NAME) : "?",
                       ok ? "loaded" : "FAILED");
                if (!ok)
                    return 0;
            }
        }
    }
    return 1;
}

// ---------------------------------------------------------------- gate entities
typedef uintptr_t (*spawn_fn)(uintptr_t game, uintptr_t def, const float *origin, const float *axis, uintptr_t owner,
                              char, uintptr_t, const float *, const float *);

static uintptr_t entity_by_id(uint32_t id)
{
    uintptr_t g = game();
    if (!g || !id || id == 0x3FFF0010u)
        return 0;
    uintptr_t e = ((uintptr_t *)(g + OFF_ENTITIES))[id >> 16];
    return e && readable(e + ENT_ID, 4) && *(uint32_t *)(e + ENT_ID) == id ? e : 0;
}

// Find a decl by type and name, as the "touchDecl" console command does.
#define FN_DECL_TYPE 0x14002FD70ull         // (declTypes, "entityDef") -> decl type
#define DECL_TYPES 0x142713660ull
#define FN_DECL_FIND 0x1400318B0ull         // (declType, name, makeDefault, noLookup) -> decl
static uintptr_t find_decl(const char *type, const char *name)
{
    uintptr_t t = ((uintptr_t (*)(uintptr_t, const char *))FN_DECL_TYPE)(DECL_TYPES, type);
    return t ? ((uintptr_t (*)(uintptr_t, const char *, char, char))FN_DECL_FIND)(t, name, 0, 0) : 0;
}

// Passing through: Blink's arrival burst at both gates, from a self-removing entity (voidgate_pass.def).
static void pass_flash(int from)
{
    // looked up every time: a load or a new map reloads the decls and an old pointer would be stale
    uintptr_t def = find_decl("entityDef", "models/powers/voidgate/voidgate_pass.def");
    if (!def)
        return;
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i == 0 ? from : from ^ 1];
        float axis[9], pos[3];
        ((dir_to_axis_fn)FN_DIR_TO_AXIS)(g->n, axis);
        for (int k = 0; k < 3; k++)
            pos[k] = g->pos[k] + g->n[k] * 0.3f;
        uintptr_t e = ((spawn_fn)FN_SPAWN)(game(), def, pos, axis, player(), 0, 0, 0, 0);
        if (!e)
            continue;
        // remembered for removal a few seconds later (cleanup_flashes): the engine's own remove-on-FX-end crashed
        int slot = 0;
        for (int k = 0; k < MAX_FLASHES; k++)
            if (!S->flash[k].id || (LONG)(S->flash[k].at - S->flash[slot].at) < 0) {
                slot = k;
                if (!S->flash[k].id)
                    break;
            }
        S->flash[slot].id = *(uint32_t *)(e + ENT_ID);
        S->flash[slot].at = GetTickCount();
    }
}

// Remove flash entities older than 3 s. Called from the cast and the power's own update (where gates are removed
// too), never from the FX update.
static uintptr_t entity_by_id(uint32_t id);
static void cleanup_flashes(void)
{
    for (int k = 0; k < MAX_FLASHES; k++) {
        if (!S->flash[k].id || GetTickCount() - S->flash[k].at < 3000)
            continue;
        uintptr_t e = entity_by_id(S->flash[k].id);
        S->flash[k].id = 0;
        if (e) {
            unsigned char result[32] = {0};
            ((uintptr_t (*)(uintptr_t, void *, uintptr_t))0x140A06270ull)(e, result, 0x143BFEAA0ull);
        }
    }
}

#define FN_ENTITY_EVENT 0x140A06270ull      // (entity, result variant*, idEventDef*) -> runs the event now
// arkCpntFX: +0x10 owner entity, +0x28 m_fx, +0x4C FX handle, +0x70 started, +0x71 stopped (update 0x14058E040)
#define FN_FX_KILL 0x1401FF7C0ull           // (fxSystem, handle): what cpntFX calls for m_stopOnTargetRemoval
#define FN_CPNTFX_STOP 0x14058DF40ull       // (cpntFX): detach the FX's end listener (+0x20) and stop it
#define FN_ENTITY_POST_EVENT 0x140A060D0ull  // (entity, idEventDef*, 0, 0) -> queued
#define EV_REMOVE 0x143BFEAA0ull            // idEventDef "remove": "Removes this object from the game."

// A gate going away (closed with F, or replaced by a recast) fades instead of vanishing: its FX stopped with the
// component's own stop (the FX's stop sound -- the Void portal's "end" -- plays, its particles end), a Void burst
// where it was, and the entity removed 3 s later by a posted event (removed at once, its sounds were cut).
static struct {
    uint32_t id;
    DWORD at;
} g_dying[8];
static void spawn_burst(const float pos[3], const float n[3])
{
    uintptr_t def = find_decl("entityDef", "models/powers/voidgate/voidgate_pass.def");
    if (!def)
        return;
    float axis[9], at[3];
    ((dir_to_axis_fn)FN_DIR_TO_AXIS)(n, axis);
    for (int k = 0; k < 3; k++)
        at[k] = pos[k] + n[k] * 0.3f;
    uintptr_t e = ((spawn_fn)FN_SPAWN)(game(), def, at, axis, player(), 0, 0, 0, 0);
    for (int k = 0; e && k < 8; k++)
        if (!g_dying[k].id) {
            g_dying[k].id = *(uint32_t *)(e + ENT_ID), g_dying[k].at = GetTickCount() + 3000;
            break;
        }
}
static void gate_fade_out(int i, uint32_t id, uintptr_t fc, const float pos[3], const float n[3])
{
    uintptr_t e = entity_by_id(id);
    if (!e)
        return;
    if (fc && readable(fc + 0x4C, 4) && *(uintptr_t *)(fc + 0x10) == e && *(uint32_t *)(fc + 0x4C) != ~0u)
        ((void (*)(uintptr_t))FN_CPNTFX_STOP)(fc);  // (see remove_gate_entity: never clear +0x4C)
    spawn_burst(pos, n);
    int k = 0;
    while (k < 7 && g_dying[k].id)
        k++;
    if (g_dying[k].id) {  // (full: the oldest goes now)
        uintptr_t old = entity_by_id(g_dying[k].id);
        if (old)
            ((char (*)(uintptr_t, uintptr_t, int, int))FN_ENTITY_POST_EVENT)(old, EV_REMOVE, 0, 0);
    }
    g_dying[k].id = id, g_dying[k].at = GetTickCount() + 3000;
    vg_log("gate %c fading out (entity %08X)", 'A' + i, id);
}
static void dying_update(void)  // game frame: remove what has faded
{
    for (int k = 0; k < 8; k++) {
        if (!g_dying[k].id || (LONG)(GetTickCount() - g_dying[k].at) < 0)
            continue;
        uintptr_t e = entity_by_id(g_dying[k].id);
        if (e)
            ((char (*)(uintptr_t, uintptr_t, int, int))FN_ENTITY_POST_EVENT)(e, EV_REMOVE, 0, 0);
        g_dying[k].id = 0;
    }
}

// Remove a gate's entity, stopping its looping FX first. Only from the cast or the power's update: removing an
// entity while the engine walks the cpntFX components (our FX update hook) could free one it is about to update.
static void remove_gate_entity(int i, uint32_t id, uintptr_t fc)
{
    uintptr_t e = entity_by_id(id);
    if (!e)
        return;
    // stop the FX with the component's own stop (what its removal calls too): it first points the FX instance's
    // end listener (+0x128) away from the component. Stopping by handle and clearing +0x4C made the removal skip
    // that, and the FX ending a moment later called back into the freed component (crash).
    if (fc && readable(fc + 0x4C, 4) && *(uintptr_t *)(fc + 0x10) == e && *(uint32_t *)(fc + 0x4C) != ~0u) {
        vg_log("gate %c FX %08X stopped", 'A' + i, *(uint32_t *)(fc + 0x4C));
        ((void (*)(uintptr_t))FN_CPNTFX_STOP)(fc);
    } else
        vg_log("gate %c FX not stopped (cpntFX %p)", 'A' + i, (void *)fc);
    unsigned char result[32] = {0};
    ((uintptr_t (*)(uintptr_t, void *, uintptr_t))FN_ENTITY_EVENT)(e, result, EV_REMOVE);
    vg_log("gate %c entity %08X removed", 'A' + i, id);
}

// Spawn each gate's entity at its gate, replacing the old one after a recast (same contexts as above).
static void gate_entities_update(void)
{
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i];
        if (!g->valid || !g->dirty)
            continue;
        g->dirty = 0;
        float axis[9], pos[3];
        ((dir_to_axis_fn)FN_DIR_TO_AXIS)(g->n, axis);
        for (int k = 0; k < 3; k++)
            pos[k] = g->pos[k] + g->n[k] * 0.12f;  // off the surface: the dark quad fades out where it touches
        // a teleported entity leaves its cpntFX behind: replace the entity instead of moving it (the old one fades)
        if (g->ent) {
            uintptr_t old = entity_by_id(g->ent);
            float *oo = old ? (float *)(old + ENT_ORIGIN) : NULL, *oa = old ? (float *)(old + ENT_AXIS) : NULL;
            if (old)
                gate_fade_out(i, g->ent, g->fxComp, oo, oa);  // (entity origin / axis row 0 = where it stood)
        }
        g->ent = 0, g->fxComp = 0;
        uintptr_t e;
        // gate A blue, gate B violet; looked up every time: a load reloads the decls, and a def kept from before it
        // crashed the spawn (an older pointer only made it return 0)
        uintptr_t def = find_decl("entityDef", i == 1 ? "models/powers/voidgate/voidgate_gate_b.def"
                                                      : "models/powers/voidgate/voidgate_gate.def");
        if (!def) {
            vg_log("gate %c: no gate entityDef", 'A' + i);
            continue;
        }
        e = ((spawn_fn)FN_SPAWN)(game(), def, pos, axis, player(), 0, 0, 0, 0);
        g->ent = e ? *(uint32_t *)(e + ENT_ID) : 0;
        vg_log("gate %c entity spawned: %p id %08X (def %p)", 'A' + i, (void *)e, g->ent, (void *)def);
    }
}

// Each gate's FX is started once (placed in world space, it stays put); a failed start is retried once a second.
static void gate_fx_update(void)
{
    DWORD now = GetTickCount();
    if (S->fxGate && S->fxGate != S->fxGateChecked) {
        S->fxGateChecked = S->fxGate;
        if (!fx_prepare(S->fxGate)) {
            vg_log("gate FX unusable, falling back to Far Reach's aim marker");
            S->fxGate = S->fxGateChecked = S->fxFallback;
        }
    }
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i];
        if (!g->valid)
            continue;
        if (!g->fx) {
            if (now >= g->fxRetryAt) {
                g->fx = fx_start(S->fxGate, g->pos, g->n);
                g->fxRetryAt = now + 1000;
                g->fxCheckAt = now + 500;
                vg_log("gate %c FX start: handle %08X (decl %p)", 'A' + i, g->fx, (void *)S->fxGate);
            }
            continue;
        }
        if (g->fxCheckAt && now >= g->fxCheckAt) {
            g->fxCheckAt = 0;
            vg_log("gate %c FX %08X alive %d", 'A' + i, g->fx, fx_alive(g->fx));
        }
    }
    for (int i = 0; i < MAX_BURSTS; i++)
        if (S->burst[i].fx && GetTickCount() >= S->burst[i].until) {
            fx_stop(S->burst[i].fx);
            S->burst[i].fx = 0;
        }
}

// one-shot FX, stopped after a few seconds in case it loops
static void fx_once(uintptr_t fxDecl, const float pos[3], const float dir[3])
{
    for (int i = 0; i < MAX_BURSTS; i++)
        if (!S->burst[i].fx) {
            S->burst[i].fx = fx_start(fxDecl, pos, dir);
            S->burst[i].until = GetTickCount() + 3000;
            return;
        }
}

static void burst(const gate_t *g) { fx_once(S->fxPass, g->pos, g->n); }

// ---------------------------------------------------------------- moving things through
// The console command takes an eye position and subtracts (eyeHeight - 0.25) before posting; the event itself
// carries the feet origin, which is what we pass.
static int post_setviewpos(uint32_t entityId, const float feet[3], float yawDeg, float pitchDeg)
{
    uintptr_t g = game();
    if (!g)
        return 0;
    InterlockedIncrement((volatile LONG *)(g + OFF_EVQ_PENDING));
    uint32_t *rec = (uint32_t *)((evq_alloc_fn)FN_EVQ_ALLOC)(g + OFF_EVQ, 0x20);
    if (!rec)
        return 0;
    float *f = (float *)rec;
    rec[0] = EV_SETVIEWPOS;
    rec[1] = entityId;
    f[2] = feet[0];
    f[3] = feet[1];
    f[4] = feet[2];
    f[5] = pitchDeg;
    f[6] = yawDeg;
    rec[7] = 0;
    return 1;
}

static void teleport_player(const float feet[3], float yawDeg, float pitchDeg)
{
    uintptr_t p = player();
    if (!p || !post_setviewpos(*(uint32_t *)(p + ENT_ID), feet, yawDeg, pitchDeg))
        return;
    float *o = (float *)(p + ENT_ORIGIN);
    vg_log("teleport from (%.2f %.2f %.2f) to feet (%.2f %.2f %.2f) yaw %.1f pitch %.1f", o[0], o[1], o[2], feet[0],
           feet[1], feet[2], yawDeg, pitchDeg);
    memcpy(S->verifyTo, feet, 12);
    S->verifyAt = GetTickCount() + 300;
    memcpy(S->rescueTo, o, 12);
    S->rescueYaw = yawDeg;
    S->rescueCheckAt = GetTickCount() + 1500;
}

// Is a body (feet origin, standard size) touching this gate?
static int touching(const gate_t *g, const float feet[3])
{
    float d[3];
    int k = gate_kind(g);
    if (k) {  // floor: stand on it; ceiling: head against it
        float z = k > 0 ? feet[2] : feet[2] + BODY_HEIGHT;
        float dx = feet[0] - g->pos[0], dy = feet[1] - g->pos[1];
        return dx * dx + dy * dy < 0.8f * 0.8f && fabsf(z - g->pos[2]) < 0.3f;
    }
    float h[3];
    wall_normal(g, h);
    d[0] = feet[0] - g->pos[0], d[1] = feet[1] - g->pos[1], d[2] = feet[2] + BODY_HEIGHT * 0.5f - g->pos[2];
    float dist = d[0] * h[0] + d[1] * h[1];         // body center distance from the wall plane
    float side = -d[0] * h[1] + d[1] * h[0];        // along the wall
    return dist < BODY_RADIUS + 0.2f && dist > -0.3f && fabsf(side) < 0.85f && fabsf(d[2]) < 1.35f;
}

// Has the player gone *into* the gate (Portal-like: standing on its rim or leaning on a wall gate does nothing)?
//   floor gate: feet over its middle, or dropping fast towards it; wall gate: pressing into it (the body can't get
//   closer to the wall than its radius) for two frames in a row; ceiling gate: head into it.
static int g_entryLog;  // test channel "entrylog 0|1": the floor-gate entry test per frame
// the player's last move between two origin samples (a fast fall can go through a floor gate between two of them)
static float g_segFrom[3], g_segTo[3];
static DWORD g_segAt;
static int player_entered(int i, const float feet[3], float vz)
{
    gate_t *g = &S->gate[i];
    int k = gate_kind(g);
    if (k > 0) {
        float dx = feet[0] - g->pos[0], dy = feet[1] - g->pos[1], h2 = dx * dx + dy * dy, dz = feet[2] - g->pos[2];
        if (h2 < 0.45f * 0.45f && fabsf(dz) < 0.25f)
            return 1;
        // the last move went down through the plane inside the oval (any speed)
        float z0 = g_segFrom[2] - g->pos[2], z1 = g_segTo[2] - g->pos[2];
        if (g_segAt && GetTickCount() - g_segAt < 150 && z0 > -0.05f && z1 < 0.25f && z0 - z1 > 0.05f &&
            dist2(g_segTo, feet) < 0.5f * 0.5f) {
            float t = z0 > 0 ? z0 / (z0 - z1) : 0, c[2];
            for (int k2 = 0; k2 < 2; k2++)
                c[k2] = g_segFrom[k2] + (g_segTo[k2] - g_segFrom[k2]) * t - g->pos[k2];
            if (c[0] * c[0] + c[1] * c[1] < 0.75f * 0.75f)
                return 1;
        }
        if (g_entryLog && h2 < 1.0f && dz > -1.5f && dz < 4.0f)
            vg_log("entry check gate %c: dz %.2f h %.2f vz %.1f", 'A' + i, dz, sqrtf(h2), vz);
        return vz < -3.0f && h2 < 0.75f * 0.75f && dz > -0.2f && dz < 0.3f - vz * 0.08f;  // ~4-5 frames ahead
    }
    if (k < 0)
        return touching(g, feet);
    float h[3], d[3];
    wall_normal(g, h);
    d[0] = feet[0] - g->pos[0], d[1] = feet[1] - g->pos[1], d[2] = feet[2] + BODY_HEIGHT * 0.5f - g->pos[2];
    float dist = d[0] * h[0] + d[1] * h[1], side = -d[0] * h[1] + d[1] * h[0];
    int pressing = dist < BODY_RADIUS + 0.06f && dist > -0.3f && fabsf(side) < 0.7f && fabsf(d[2]) < 1.2f;
    S->wallPress[i] = pressing ? S->wallPress[i] + 1 : 0;
    return S->wallPress[i] >= 2;
}

// ---------------------------------------------------------------- player vertical speed (momentum)
// arkCpntPlayerPhysics (type descriptor 0x143C28CF0, GetComponent 0x1409FAAC0): [cpnt+0x48]+0x50 = vertical speed,
// found live by correlating with d(origin z)/dt while jumping (tools/d2vel.py). Horizontal speed: not found yet.
#define FN_GET_COMPONENT 0x1409FAAC0ull
#define TYPE_PLAYER_PHYSICS 0x143C28CF0ull
static float *player_vz_ptr(void)
{
    uintptr_t p = player();
    if (!p)
        return NULL;
    uintptr_t c = ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(p, TYPE_PLAYER_PHYSICS, 2);
    if (!c || !readable(c + 0x48, 8))
        return NULL;
    uintptr_t st = *(uintptr_t *)(c + 0x48);  // the 0x141EE9AF8 subobject (+0x30) has it at +0x18
    return readable(st + 0x50, 4) ? (float *)(st + 0x50) : NULL;
}

static int touching_any(const float feet[3])
{
    for (int i = 0; i < 2; i++)
        if (touching(&S->gate[i], feet))
            return i;
    return -1;
}

// ---------------------------------------------------------------- through a gate (vg_portal.h: the Portal map)
#define GATE_HALF_W 0.85f  // the oval: 1.7 m across, 2.8 m along its up
#define GATE_HALF_H 1.4f
static void gate_frame(const gate_t *g, vg_frame *f)
{
    float up[3] = {g->up[0], g->up[1], g->up[2]};
    if (dot3(up, up) < 1e-6f)  // (gates from before the up axis)
        up[0] = 0, up[1] = 0, up[2] = 1;
    vg_frame_make(f, g->pos, g->n, up);
}

// Where a body (feet origin) that went into gate `from` comes out, Portal-like: at the same place across the gate,
// kept inside the oval and in front of the exit surface, the view turned with the gates (yaw/pitch in place).
static void exit_pose(int from, const float feet[3], float out[3], float *yaw, float *pitch)
{
    gate_t *a = &S->gate[from], *b = &S->gate[from ^ 1];
    vg_frame fa, fb;
    gate_frame(a, &fa), gate_frame(b, &fb);
    float hh = BODY_HEIGHT * 0.5f, c[3] = {feet[0], feet[1], feet[2] + hh};
    // on a floor or ceiling the oval's long axis need not match our up (the FX picks its own): keep inside the
    // narrow half both ways there
    float halfH = gate_kind(b) ? GATE_HALF_W : GATE_HALF_H;
    vg_exit_body(&fa, &fb, c, BODY_RADIUS, hh, GATE_HALF_W, halfH, 0.05f, out);
    out[2] -= hh;
    // a low wall gate would put the feet under the floor: the floor near it is assumed to be where the caster stood
    if (gate_kind(b) == 0 && out[2] < b->ground + 0.05f && b->pos[2] > b->ground - 0.5f)
        out[2] = b->ground + 0.05f;
    // the whole camera frame turned (forward and up): looking up or down out of a floor / ceiling gate, a yaw taken
    // from the forward alone could come out 180 degrees off -- the picture upside down (user report)
    float f[3], u[3], f2[3], u2[3];
    vg_angles_to_fwd(*yaw, *pitch, f);
    vg_angles_to_up(*yaw, *pitch, u);
    vg_map_dir(&fa, &fb, f, f2);
    vg_map_dir(&fa, &fb, u, u2);
    vg_view_to_angles(f2, u2, yaw, pitch);
    *pitch = *pitch > 85 ? 85 : *pitch < -85 ? -85 : *pitch;
}

// the player's velocity: the physics vector while airborne, else measured from the origin (walking, running)
static void player_velocity(float v[3])
{
    float *vz = player_vz_ptr();
    if (vz && vz[-2] * vz[-2] + vz[-1] * vz[-1] + vz[0] * vz[0] > 0.3f * 0.3f)
        v[0] = vz[-2], v[1] = vz[-1], v[2] = vz[0];
    else
        v[0] = S->vEst[0], v[1] = S->vEst[1], v[2] = S->vEst[2];
}

// the last pass through the gates (the escape on F: back to where the player went in)
static DWORD g_lastPassAt;
static float g_lastEntry[3], g_lastEntryYaw, g_lastExit[3];
static void player_through(int from)
{
    float eye[3], fwd[3], out[3];
    uintptr_t p = player();
    if (!p || !player_view(eye, fwd))
        return;
    float yaw, pitch;
    vg_fwd_to_angles(fwd, &yaw, &pitch);
    float entryYaw, entryPitch;
    vg_fwd_to_angles(fwd, &entryYaw, &entryPitch);
    memcpy(g_lastEntry, (float *)(p + ENT_ORIGIN), 12), g_lastEntryYaw = entryYaw;
    exit_pose(from, (float *)(p + ENT_ORIGIN), out, &yaw, &pitch);
    memcpy(g_lastExit, out, 12), g_lastPassAt = GetTickCount();
    teleport_player(out, yaw, pitch);
    pass_flash(from);
    safe_landing_start();  // no fall damage until the player has landed after the exit
    // momentum: the velocity turns with the gates (fall into a floor gate, fly out of a wall one)
    vg_frame fa, fb;
    gate_frame(&S->gate[from], &fa), gate_frame(&S->gate[from ^ 1], &fb);
    float v[3], vo[3];
    player_velocity(v);
    vg_map_dir(&fa, &fb, v, vo);
    S->pendingVOn = 0;
    if (dot3(vo, vo) > 2.0f * 2.0f) {
        memcpy(S->pendingV, vo, 12);
        S->pendingVOn = 1, S->pendingVzFrames = 0;
        S->pendingVzUntil = GetTickCount() + 600;
        vg_log("momentum: in (%.1f %.1f %.1f) -> out (%.1f %.1f %.1f), %.1f m/s", v[0], v[1], v[2], vo[0], vo[1], vo[2],
               sqrtf(dot3(vo, vo)));
    }
}

// The teleport is an event applied a frame later and it resets the speed: keep pushing the exit velocity until the
// player is at the exit (and a couple of frames after). Only takes while airborne (the ground movement has its own).
static void apply_momentum(const float feet[3])
{
    if (!S->pendingVOn)
        return;
    if (GetTickCount() > S->pendingVzUntil) {
        S->pendingVOn = 0;
        return;
    }
    float d[3] = {feet[0] - S->verifyTo[0], feet[1] - S->verifyTo[1], feet[2] - S->verifyTo[2]};
    if (dot3(d, d) > 1.5f * 1.5f)
        return;  // not moved yet
    float *vz = player_vz_ptr();
    if (vz)
        vz[-2] = S->pendingV[0], vz[-1] = S->pendingV[1], vz[0] = S->pendingV[2];
    if (++S->pendingVzFrames >= 3)
        S->pendingVOn = 0, S->pendingVzFrames = 0;
}

static int recently_passed(uint32_t id)
{
    for (int i = 0; i < MAX_PASSED; i++)
        if (S->passed[i].id == id)
            return 1;
    return 0;
}

static void mark_passed(uint32_t id)
{
    int slot = 0;
    for (int i = 0; i < MAX_PASSED; i++) {
        if (!S->passed[i].id) {
            slot = i;
            break;
        }
        if (S->passed[i].since < S->passed[slot].since)
            slot = i;  // full: reuse the oldest
    }
    S->passed[slot].id = id;
    S->passed[slot].since = GetTickCount();
}

// The entity's own "teleport" event (format "va": position, angles) runs its class's Teleport -- for an NPC that is
// the AI's, not just idEntity::Teleport_r (which moved NPCs but sometimes left them invisible).
#define EV_TELEPORT 0x143C02990ull
#define FN_ENTITY_EVENT_ARGS 0x140A06190ull // (entity, result*, idEventDef*, argc, args...)
static void entity_teleport_event(uintptr_t e, const float out[3], float yaw)
{
    float pos[3] = {out[0], out[1], out[2]}, ang[3] = {0, yaw, 0};  // idAngles: pitch, yaw, roll
    unsigned char result[32] = {0};
    ((uintptr_t (*)(uintptr_t, void *, uintptr_t, int, ...))FN_ENTITY_EVENT_ARGS)(e, result, EV_TELEPORT, 2, pos, ang);
}

// ---------------------------------------------------------------- NPCs: falling, momentum (docs/d2_npc_fall_research.md)
// A live NPC has no falling animation: the engine's only fall reaction is a ragdoll, and the AI keeps it from
// starting for walking NPCs, so an NPC sent out of a gate high up walked on air. As the engine does for a fall /
// throw hit: ARK_MSG_START_RAGDOLL, then (once the ragdoll runs) ARK_MSG_VELOCITY_IMPULSE with the velocity turned
// through the gates. Bodies already ragdolled (corpses, knocked out) only get the impulse (the teleport zeroed them).
static int g_npcFall = 1;  // ragdoll + momentum for NPCs coming out of gates ("npcfall")
#define MSG_START_RAGDOLL 0xB9
#define MSG_VELOCITY_IMPULSE 0x149
#define ENT_LIN_VEL 0x33C                   // entity: linear velocity (world, m/s)
#define ENT_DEF 0x2E8  // entity: its entityDef (found next to a fresh spawn, test channel "spawnview" -> defOff)
#define TYPE_PHYSICS_SYSTEM 0x143C0B1A0ull  // arkCpntPhysicsSystemEvolved: +0x238 mode (0 / 2 = ragdoll)
static void post_entity_msg(uint32_t msg, uint32_t handle, const void *payload, uint32_t size)
{
    uintptr_t g = game();
    if (!g)
        return;
    InterlockedIncrement((volatile LONG *)(g + OFF_EVQ_PENDING));
    uint32_t *m = (uint32_t *)((evq_alloc_fn)FN_EVQ_ALLOC)(g + OFF_EVQ, (int)size + 8);
    if (!m)
        return;
    m[0] = (size << 16) | msg, m[1] = handle;
    memcpy(m + 2, payload, size);
}

static int ragdoll_mode(uintptr_t e)  // 1 ragdolling, 0 animated, -1 unknown
{
    uintptr_t ps = ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(e, TYPE_PHYSICS_SYSTEM, 2);
    if (!ps || !readable(ps + 0x238, 4))
        return -1;
    int mode = *(int *)(ps + 0x238);
    return mode == 0 || mode == 2;
}

static struct {
    uint32_t id;
    DWORD until;
    float v[3];
} g_npcImpulse[8];  // velocity impulses waiting for the ragdoll to start

static void velocity_impulse(uint32_t id, const float v[3])
{
    struct {
        float lin[3], ang[3];
        uint8_t playerInstigator, additive, pad[2];
    } vi = {{v[0], v[1], v[2]}, {0, 0, 0}, 0, 0, {0, 0}};
    post_entity_msg(MSG_VELOCITY_IMPULSE, id, &vi, 0x1C);
}

static void npc_impulses(void)  // game tick
{
    for (int i = 0; i < 8; i++) {
        if (!g_npcImpulse[i].id)
            continue;
        uintptr_t e = entity_by_id(g_npcImpulse[i].id);
        if (!e) {
            g_npcImpulse[i].id = 0;
            continue;
        }
        int late = (LONG)(GetTickCount() - g_npcImpulse[i].until) > 0;
        if (ragdoll_mode(e) == 1 || late) {
            velocity_impulse(g_npcImpulse[i].id, g_npcImpulse[i].v);
            vg_log("entity %08X: velocity impulse (%.1f %.1f %.1f)%s", g_npcImpulse[i].id, g_npcImpulse[i].v[0],
                   g_npcImpulse[i].v[1], g_npcImpulse[i].v[2], late ? " (ragdoll not seen)" : "");
            g_npcImpulse[i].id = 0;
        }
    }
}

static void npc_after_teleport(uintptr_t e, int from, const float vout[3])
{
    uint32_t id = *(uint32_t *)(e + ENT_ID);
    int rag = ragdoll_mode(e);
    gate_t *b = &S->gate[from ^ 1];
    float speed = sqrtf(dot3(vout, vout));
    int airborne = gate_kind(b) <= 0 && b->pos[2] - b->ground > 1.0f;  // a wall / ceiling exit well above the floor
    if (rag != 0) {  // already a ragdoll (body, knocked out), or a loose object: keep its momentum
        if (speed > 0.5f) {
            velocity_impulse(id, vout);
            vg_log("entity %08X: momentum %.1f m/s through the gates", id, speed);
        }
        return;
    }
    if (rag == 0 && (airborne || speed > 6.0f)) {
        struct {
            uintptr_t activeRagdoll;
            int type, pad;
        } sr = {0, 0, 0};  // plain full ragdoll, now
        post_entity_msg(MSG_START_RAGDOLL, id, &sr, 0x10);
        vg_log("entity %08X: ragdoll (exit %s, %.1f m/s)", id, airborne ? "in the air" : "fast", speed);
        for (int i = 0; i < 8; i++)
            if (!g_npcImpulse[i].id) {
                g_npcImpulse[i].id = id, g_npcImpulse[i].until = GetTickCount() + 400;
                memcpy(g_npcImpulse[i].v, vout, 12);
                break;
            }
    }
}

// Soft landing (user: low chaos; the stock rule kills an NPC that falls > 4 m as a ragdoll, so a high gate killed).
// The ragdoll contact callback (0x140873F10) hurts only when now - lastFallDamage(physSys+0x3C0, double) > 0.3 s:
// height damage when refZ(physSys+0x3AC) - z > 4 m (instant kill), else impact speed > 13 m/s (up to 40). For an NPC
// or a body that came out of a gate: lastFallDamage = far future (no fall damage at all) and refZ = its z (no stale
// height for later), until it has been still for a second (6 s at least, 20 s at most); then both are restored.
static int g_npcSoft = 1;  // ("npcsoft")
#define PS_REF_Z 0x3AC
#define PS_LAST_FALL_DAMAGE 0x3C0
static struct {
    uint32_t id;
    DWORD from, stillSince;
    double lastDamage;
    float topZ;
} g_soft[8];

static void npc_soft_add(uint32_t id)
{
    if (!g_npcSoft)
        return;
    int k = -1;
    for (int i = 0; i < 8; i++)
        if (g_soft[i].id == id || (k < 0 && !g_soft[i].id))
            k = i;
    if (k < 0)
        k = 0;  // (full: the oldest goes unprotected)
    if (g_soft[k].id != id)
        g_soft[k].id = id, g_soft[k].lastDamage = -1, g_soft[k].topZ = -1e9f;
    g_soft[k].from = GetTickCount(), g_soft[k].stillSince = 0;
}

static void npc_soft_landing(void)  // every frame
{
    for (int i = 0; i < 8; i++) {
        if (!g_soft[i].id)
            continue;
        uintptr_t e = entity_by_id(g_soft[i].id);
        uintptr_t ps = e ? ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(e, TYPE_PHYSICS_SYSTEM, 2) : 0;
        if (!ps || !readable(ps + PS_LAST_FALL_DAMAGE, 8)) {
            g_soft[i].id = 0;
            continue;
        }
        float z = ((float *)(e + ENT_ORIGIN))[2], *v = (float *)(e + ENT_LIN_VEL);
        double *last = (double *)(ps + PS_LAST_FALL_DAMAGE);
        if (g_soft[i].lastDamage < 0)
            g_soft[i].lastDamage = *last < 1e30 ? *last : 0;
        if (z > g_soft[i].topZ)
            g_soft[i].topZ = z;
        *last = 1e300;                    // no fall damage
        *(float *)(ps + PS_REF_Z) = z;   // and no height stored for later
        DWORD now = GetTickCount();
        if (dot3(v, v) > 2.0f * 2.0f)
            g_soft[i].stillSince = 0;
        else if (!g_soft[i].stillSince)
            g_soft[i].stillSince = now;
        int over = (now - g_soft[i].from > 6000 && g_soft[i].stillSince && now - g_soft[i].stillSince > 1000) ||
                   now - g_soft[i].from > 20000;
        if (!g_npcSoft || over) {
            *last = g_soft[i].lastDamage;
            vg_log("entity %08X: soft landing over (fell %.1f m from its highest point after the gate)", g_soft[i].id,
                   g_soft[i].topZ - z);
            g_soft[i].id = 0;
        }
    }
}

static void entity_through(uintptr_t e, int from, int cls)
{
    float *feet = (float *)(e + ENT_ORIGIN), *fwd = (float *)(e + ENT_AXIS);
    float yaw = yaw_of(fwd), pitch = 0, out[3], dir[3], axis[9];
    uint32_t id = *(uint32_t *)(e + ENT_ID);
    // its velocity turned through the gates (read before the teleport)
    float vout[3] = {0, 0, 0};
    if (readable(e + ENT_LIN_VEL, 12)) {
        vg_frame fa, fb;
        gate_frame(&S->gate[from], &fa), gate_frame(&S->gate[from ^ 1], &fb);
        vg_map_dir(&fa, &fb, (float *)(e + ENT_LIN_VEL), vout);
    }
    exit_pose(from, feet, out, &yaw, &pitch);
    if (class_in(cls, CLASS_NPC))
        npc_soft_add(id);  // (alive, knocked out or dead: a body can die from a fall too)
    static const char *how[] = {"setviewpos event", "Teleport_r", "teleport event"};
    int mode = S->npcDirect < 0 || S->npcDirect > 2 ? 0 : S->npcDirect;
    vg_log("entity %08X (class %d) through gate %c: (%.2f %.2f %.2f) -> (%.2f %.2f %.2f) [%s]", id, cls, 'A' + from,
           feet[0], feet[1], feet[2], out[0], out[1], out[2], how[mode]);
    if (mode == 1) {
        dir[0] = cosf(yaw / 57.2957795f), dir[1] = sinf(yaw / 57.2957795f), dir[2] = 0;
        ((dir_to_axis_fn)FN_DIR_TO_AXIS)(dir, axis);
        ((teleport_fn)FN_TELEPORT)(e, out, axis, 0, 0);
        *(unsigned char *)(e + 0xD4) |= 4;  // set by the scripts' Teleport node (0x1408E7E00) right after Teleport_r
        if (g_npcFall)
            npc_after_teleport(e, from, vout);
    } else if (mode == 2)
        entity_teleport_event(e, out, yaw);
    else
        post_setviewpos(id, out, yaw, 0);
    // check where it ended up a bit later (and fall back to its own teleport event if the deferred one did nothing)
    for (int i = 0; i < 4; i++)
        if (!S->npcCheck[i].id || i == 3) {
            S->npcCheck[i].id = id, S->npcCheck[i].at = GetTickCount() + 400, S->npcCheck[i].yaw = yaw;
            memcpy(S->npcCheck[i].to, out, 12);
            break;
        }
    mark_passed(id);
    pass_flash(from);
}

static void check_npc_teleports(void)
{
    for (int i = 0; i < 4; i++) {
        if (!S->npcCheck[i].id || GetTickCount() < S->npcCheck[i].at)
            continue;
        uintptr_t e = entity_by_id(S->npcCheck[i].id);
        if (e) {
            float *o = (float *)(e + ENT_ORIGIN);
            float d2 = dist2(o, S->npcCheck[i].to);
            // render entity (+0x358) flags as cpntFX reads them for "visible" (+0x1C bits 2-3, +0x408 bit 4)
            uintptr_t r = *(uintptr_t *)(e + 0x358);
            vg_log("entity %08X after teleport: (%.2f %.2f %.2f), %.2f m from the exit; flags d0 %08X d4 %08X, "
                   "render %p %08X %08X", S->npcCheck[i].id, o[0], o[1], o[2], sqrtf(d2), *(uint32_t *)(e + 0xD0),
                   *(uint32_t *)(e + 0xD4), (void *)r, r && readable(r + 0x1C, 4) ? *(uint32_t *)(r + 0x1C) : 0,
                   r && readable(r + 0x408, 4) ? *(uint32_t *)(r + 0x408) : 0);
            if (d2 > 2.5f * 2.5f && S->npcDirect == 0) {
                vg_log("  deferred teleport did not move it: its own teleport event");
                entity_teleport_event(e, S->npcCheck[i].to, S->npcCheck[i].yaw);
            }
        }
        S->npcCheck[i].id = 0;
    }
}

// NPCs and loose physics objects near the gates
// Will this body cross gate i before the next look (its velocity over `dt`)? From the gate's front side through
// its plane, inside the oval: fast things (a falling body, a thrown object) go through before they hit the surface
// behind the gate (by the time they touch it they would have stopped on it, momentum lost).
static int crossing(int i, const float o[3], const float v[3], float dt)
{
    gate_t *g = &S->gate[i];
    if (!g->valid)
        return 0;
    float d0 = (o[0] - g->pos[0]) * g->n[0] + (o[1] - g->pos[1]) * g->n[1] + (o[2] - g->pos[2]) * g->n[2];
    float vn = dot3(v, g->n);
    if (d0 < -0.05f || d0 > 3.0f || vn > -1.0f)
        return 0;  // behind it, far, or not moving into it
    float t = d0 / -vn;  // time to the plane
    if (t > dt)
        return 0;
    float hit[3] = {o[0] + v[0] * t, o[1] + v[1] * t, o[2] + v[2] * t}, l[3];
    vg_frame f;
    gate_frame(g, &f);
    vg_to_local(&f, hit, l);
    float a = l[0] / GATE_HALF_H, b = l[1] / GATE_HALF_W;
    return a * a + b * b < 1.0f;
}

// Projectiles (crossbow bolts, darts, grenades: class 1853 seen for a bolt): carried across at the point where
// they cross the gate's plane, their velocity and their axis turned with the gates. Taken at the last scan before
// they reach the plane (a bolt flies 100 m/s: 3 m per scan).
#define CLASS_PROJECTILE 1853
static void projectile_through(uintptr_t e, int from, const float v[3])
{
    gate_t *a = &S->gate[from], *b = &S->gate[from ^ 1];
    vg_frame fa, fb;
    gate_frame(a, &fa), gate_frame(b, &fb);
    float *o = (float *)(e + ENT_ORIGIN), axis[9], out[3], vout[3], cross[3];
    float d0 = dot3(o, a->n) - dot3(a->pos, a->n), vn = dot3(v, a->n), t = vn < -1e-3f ? d0 / -vn : 0;
    for (int k = 0; k < 3; k++)
        cross[k] = o[k] + v[k] * t;
    vg_map_point(&fa, &fb, cross, out);
    vg_map_dir(&fa, &fb, v, vout);
    for (int k = 0; k < 3; k++)
        out[k] += b->n[k] * 0.15f;  // just out of B's plane
    for (int r = 0; r < 3; r++)
        vg_map_dir(&fa, &fb, (float *)(e + ENT_AXIS) + 3 * r, axis + 3 * r);
    uint32_t id = *(uint32_t *)(e + ENT_ID);
    ((teleport_fn)FN_TELEPORT)(e, out, axis, 0, 0);
    *(unsigned char *)(e + 0xD4) |= 4;
    memcpy((void *)(e + ENT_LIN_VEL), vout, 12);
    velocity_impulse(id, vout);
    mark_passed(id);
    vg_log("projectile %08X through gate %c at %.0f m/s: (%.2f %.2f %.2f) -> (%.2f %.2f %.2f) v (%.1f %.1f %.1f)", id,
           'A' + from, sqrtf(dot3(v, v)), cross[0], cross[1], cross[2], out[0], out[1], out[2], vout[0], vout[1],
           vout[2]);
}

// Fast projectiles: the pistol's bullet (an arkProjectile too, 250 m/s) flies 7.5 m between two scans and hits the
// wall behind a gate before a scan sees it. Every frame: whatever will cross a gate's plane inside the oval within
// 0.1 s, with nothing in the way up to there (a ray), goes through now.
static float ray_clear(const float o[3], const float d[3], float len);
static float ray_clear_static(const float o[3], const float d[3], float len);

// Windblast: the power spawns a wind entity (windblast_object.def: cpntWindBlast, a wave out to 8 m at 30 m/s) at
// the hand, facing the aim; spawned anywhere it blows whatever is in front of it (tested: loose objects flew 4-6 m).
// A gate in the wave's way (in front of it, within 8 m, inside a 35-degree cone, nothing in between) sends a copy
// out of the other gate: the same def spawned there, turned with the gates.
#define WIND_RANGE 8.0f
static uintptr_t g_windDef[4], g_viewDefNow;  // (looked up again for every new player: a load reloads decls)
static uint32_t g_windSeen[8], g_windOurs[8];
static void wind_seen(uintptr_t e, uintptr_t def)
{
    uint32_t id = *(uint32_t *)(e + ENT_ID);
    for (int k = 0; k < 8; k++)
        if (g_windSeen[k] == id || g_windOurs[k] == id)
            return;
    memmove(g_windSeen + 1, g_windSeen, sizeof g_windSeen - sizeof g_windSeen[0]);
    g_windSeen[0] = id;
    float *o = (float *)(e + ENT_ORIGIN), *f = (float *)(e + ENT_AXIS);  // axis row 0: forward
    for (int k = 0; k < 2; k++) {
        gate_t *a = &S->gate[k], *b = &S->gate[k ^ 1];
        float d[3] = {a->pos[0] - o[0], a->pos[1] - o[1], a->pos[2] - o[2]}, dist = sqrtf(dot3(d, d));
        if (dist > WIND_RANGE || dist < 0.3f || dot3(d, a->n) > -0.1f * dist || dot3(d, f) < 0.819f * dist)
            continue;  // far, behind or edge-on to the gate, out of the cone
        float dir[3] = {d[0] / dist, d[1] / dist, d[2] / dist}, free = ray_clear_static(o, dir, dist);
        if (free < dist - 0.3f) {
            vg_log("windblast %08X: something %.2f m ahead, before gate %c (%.2f m)", id, free, 'A' + k, dist);
            continue;
        }
        vg_frame fa, fb;
        gate_frame(a, &fa), gate_frame(b, &fb);
        float out[3], fwd[3], axis[9];
        vg_map_dir(&fa, &fb, f, fwd);
        for (int c = 0; c < 3; c++)
            out[c] = b->pos[c] + b->n[c] * 0.3f;
        ((dir_to_axis_fn)FN_DIR_TO_AXIS)(fwd, axis);
        uintptr_t c = ((spawn_fn)FN_SPAWN)(game(), def, out, axis, player(), 0, 0, 0, 0);
        if (c) {
            memmove(g_windOurs + 1, g_windOurs, sizeof g_windOurs - sizeof g_windOurs[0]);
            g_windOurs[0] = *(uint32_t *)(c + ENT_ID);
        }
        vg_log("windblast %08X through gate %c (%.1f m away): out of %c at (%.2f %.2f %.2f) dir (%.2f %.2f %.2f) -> %08X",
               id, 'A' + k, dist, 'A' + (k ^ 1), out[0], out[1], out[2], fwd[0], fwd[1], fwd[2], c ? g_windOurs[0] : 0);
        break;
    }
}

// an entity's def name, for logs: the first pointer in its decl that leads to a path-like string
static const char *def_name(uintptr_t e)
{
    uintptr_t d = readable(e + ENT_DEF, 8) ? *(uintptr_t *)(e + ENT_DEF) : 0;
    for (int k = 0; d && k < 0x60 && readable(d + k, 8); k += 8) {
        const char *s = *(const char **)(d + k);
        if (s && readable((uintptr_t)s, 8) && s[0] >= 'a' && s[0] <= 'z' && readable((uintptr_t)s, 64) && strchr(s, '/'))
            return s;
    }
    return "?";
}

static volatile DWORD g_newEntsUntil;  // test channel "newents ms": log entities appearing near the player
static void log_new_entities(uintptr_t g, uintptr_t p)
{
    static uint32_t known[MAX_ENTITIES];
    static int primed;
    int logging = g_newEntsUntil && GetTickCount() < g_newEntsUntil;
    if (!logging) {
        primed = 0;
        return;
    }
    for (int i = 1; i < MAX_ENTITIES; i++) {
        uintptr_t e = ((uintptr_t *)(g + OFF_ENTITIES))[i];
        uint32_t id = e && readable(e + ENT_LIN_VEL, 12) ? *(uint32_t *)(e + ENT_ID) : 0;
        if (id == known[i])
            continue;
        known[i] = id;
        if (!primed || !id)
            continue;
        float *o = (float *)(e + ENT_ORIGIN);
        if (dist2(o, (float *)(p + ENT_ORIGIN)) < 15.0f * 15.0f)
            vg_log("new entity %08X class %d at (%.2f %.2f %.2f) %.80s", id, entity_class(e), o[0], o[1], o[2],
                   def_name(e));
    }
    primed = 1;
}

static void fast_projectiles(void)
{
    uintptr_t g = game(), p = player();
    if (!g || !p)
        return;
    // once a frame is enough (the tick runs 3-4 times per frame): at most every 8 ms
    static LARGE_INTEGER last;
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now), QueryPerformanceFrequency(&freq);
    if (now.QuadPart - last.QuadPart < freq.QuadPart / 125)
        return;
    last = now;
    log_new_entities(g, p);
    static uintptr_t defsFor;
    if (defsFor != p) {
        defsFor = p;
        // the power's m_windblast{Basic,Greater,Shockwave,GreaterShockwave}EntityDef (by its upgrades: the user's
        // Corvo casts the greater shockwave one)
        static const char *wind[4] = {"windblast_object", "windblast_greater_object", "windblast_shockwave_object",
                                      "windblast_greater_shockwave_object"};
        for (int k = 0; k < 4; k++) {
            char nm[96];
            snprintf(nm, sizeof nm, "models/powers/windblast/%s.def", wind[k]);
            g_windDef[k] = find_decl("entityDef", nm);
        }
        g_viewDefNow = find_decl("entityDef", "models/powers/voidgate/voidgate_view_rect.def");
    }
    uintptr_t *ents = (uintptr_t *)(g + OFF_ENTITIES);
    for (int i = 1; i < MAX_ENTITIES; i++) {
        uintptr_t e = ents[i];
        if (!e || e == p || (e & 7))
            continue;
        float *o = (float *)(e + ENT_ORIGIN), *vel = (float *)(e + ENT_LIN_VEL);
        if (dist2(o, S->gate[0].pos) > 40.0f * 40.0f && dist2(o, S->gate[1].pos) > 40.0f * 40.0f)
            continue;
        // (no VirtualQuery here: a live entity of the table is a valid object -- a query per entity per tick cost
        // thousands of system calls a frame, the frame rate fell)
        if ((*(uint32_t *)(e + ENT_ID) >> 16) != (uint32_t)i)
            continue;
        uintptr_t def = *(uintptr_t *)(e + ENT_DEF);
        if (def && (def == g_windDef[0] || def == g_windDef[1] || def == g_windDef[2] || def == g_windDef[3])) {
            if (HAS_UPG(UPG_CROSSFIRE))
                wind_seen(e, def);
            continue;
        }
        float v2 = dot3(vel, vel);
        if (v2 < 60.0f * 60.0f || !HAS_UPG(UPG_CROSSFIRE) || entity_class(e) != CLASS_PROJECTILE)
            continue;
        uint32_t id = *(uint32_t *)(e + ENT_ID);
        if (recently_passed(id))
            continue;
        for (int k = 0; k < 2; k++) {
            gate_t *gt = &S->gate[k];
            float d0 = dot3(o, gt->n) - dot3(gt->pos, gt->n), vn = dot3(vel, gt->n);
            if (d0 < -0.05f || vn > -1.0f || d0 / -vn > 0.1f)
                continue;
            float t = d0 / -vn, hit[3], l[3];
            for (int c = 0; c < 3; c++)
                hit[c] = o[c] + vel[c] * t;
            vg_frame f;
            gate_frame(gt, &f);
            vg_to_local(&f, hit, l);
            if ((l[0] / GATE_HALF_H) * (l[0] / GATE_HALF_H) + (l[1] / GATE_HALF_W) * (l[1] / GATE_HALF_W) >= 1.0f)
                continue;
            float sp = sqrtf(v2), dir[3] = {vel[0] / sp, vel[1] / sp, vel[2] / sp}, len = sp * t;
            float free = len > 0.05f ? ray_clear(o, dir, len) : len;
            if (free < len - 0.05f) {
                vg_log("projectile %08X: something %.2f m ahead, before gate %c (%.2f m)", id, free, 'A' + k, len);
                mark_passed(id);  // (once)
                break;
            }
            projectile_through(e, k, vel);
            break;
        }
    }
}

static void scan_entities(void)
{
    fast_projectiles();
    static DWORD last;
    if (GetTickCount() - last < 30)
        return;
    last = GetTickCount();
    uintptr_t g = game(), p = player();
    if (!g || !p)
        return;
    uintptr_t *ents = (uintptr_t *)(g + OFF_ENTITIES);
    float *pf = (float *)(p + ENT_ORIGIN);
    int touchingIds = 0;
    uint32_t touchingList[MAX_PASSED];
    for (int i = 1; i < MAX_ENTITIES; i++) {
        uintptr_t e = ents[i];
        if (!e || e == p || (e & 7))
            continue;
        float *o = (float *)(e + ENT_ORIGIN);
        if (dist2(o, S->gate[0].pos) > 6.0f * 6.0f && dist2(o, S->gate[1].pos) > 6.0f * 6.0f)
            continue;
        if (!readable(e, ENT_AXIS + 36) || (*(uint32_t *)(e + ENT_ID) >> 16) != (uint32_t)i)
            continue;
        if (dist2(o, pf) < 1.2f * 1.2f)  // whatever the player carries or wields stays with the player
            continue;
        // fast and about to go through (before the next look and a bit): no need to wait until it touches
        float *vel = (float *)(e + ENT_LIN_VEL);
        int fast = dot3(vel, vel) > 4.0f * 4.0f;
        float look = dot3(vel, vel) > 30.0f * 30.0f ? 0.06f : 0.08f;  // (fast ones: not too early)
        int from = crossing(0, o, vel, look) ? 0 : crossing(1, o, vel, look) ? 1 : -1;
        if (from < 0 && (dist2(o, S->gate[0].pos) < 3.0f * 3.0f || dist2(o, S->gate[1].pos) < 3.0f * 3.0f)) {
            from = touching_any(o);
            // "touching" is sized for a body: a small thing (grenade, bottle) went through a wall gate 1 m early
            if (from >= 0 && !class_in(entity_class(e), CLASS_NPC) && gate_kind(&S->gate[from]) == 0 &&
                dot3(o, S->gate[from].n) - dot3(S->gate[from].pos, S->gate[from].n) > 0.3f)
                from = -1;
            // a body moving along a wall gate, not into it (out of a high gate with no speed, it falls down past its
            // face: taken back in, it looped between the gates and the stale fall height killed it)
            if (from >= 0 && gate_kind(&S->gate[from]) == 0 && dot3(vel, vel) > 0.5f * 0.5f &&
                dot3(vel, S->gate[from].n) > -0.3f)
                from = -1;
        }
        if (from < 0)
            continue;
        uint32_t id = *(uint32_t *)(e + ENT_ID);
        if (id == S->gate[0].ent || id == S->gate[1].ent || is_window_id(id) ||
            *(uintptr_t *)(e + ENT_DEF) == g_viewDefNow)  // (a window piece on its way out)
            continue;
        if (touchingIds < MAX_PASSED)
            touchingList[touchingIds++] = id;
        if (recently_passed(id))
            continue;
        int cls = entity_class(e);
        int seen = 0;
        for (int k = 0; k < 16; k++)
            seen |= S->loggedIds[k] == id;
        if (!seen) {
            memmove(S->loggedIds + 1, S->loggedIds, sizeof S->loggedIds - sizeof S->loggedIds[0]);
            S->loggedIds[0] = id;
            vg_log("entity %08X touches gate %c: class %d, %.80s", id, 'A' + from, cls, def_name(e));
        }
        // Through the Void: people, bodies, thrown things (grenades too); Crossfire: bolts, bullets (and the wind)
        if (class_in(cls, CLASS_NPC) || class_in(cls, CLASS_MOVABLE_A) || class_in(cls, CLASS_MOVABLE_B)) {
            if (HAS_UPG(UPG_THROUGH))
                entity_through(e, from, cls);
        } else if (cls == CLASS_PROJECTILE && fast) {
            if (HAS_UPG(UPG_CROSSFIRE))
                projectile_through(e, from, vel);
        }
        else if (fast)
            vg_log("entity %08X class %d crosses gate %c at %.1f m/s (not taken through)", id, cls, 'A' + from,
                   sqrtf(dot3(vel, vel)));
    }
    // forget entities that have left both gates (or vanished)
    for (int i = 0; i < MAX_PASSED; i++) {
        if (!S->passed[i].id || GetTickCount() - S->passed[i].since < 600)
            continue;
        int still = 0;
        for (int k = 0; k < touchingIds; k++)
            still |= touchingList[k] == S->passed[i].id;
        if (!still)
            S->passed[i].id = 0;
    }
}

// ---------------------------------------------------------------- gates
static void place_gate(const float pos[3], const float n[3], float ground)
{
    gate_t *gt = &S->gate[S->next];
    gt->dirty = 1;  // its entity follows on the next update
    memcpy(gt->pos, pos, 12);
    float l = sqrtf(dot3(n, n));
    if (l > 0.5f)
        gt->n[0] = n[0] / l, gt->n[1] = n[1] / l, gt->n[2] = n[2] / l;
    else
        gt->n[0] = 0, gt->n[1] = 0, gt->n[2] = 1;
    gt->ground = ground;
    // its up: world up on a wall; on a floor or ceiling the caster's horizontal facing (the oval's top away)
    float eye[3], fwd[3] = {1, 0, 0};
    player_view(eye, fwd);
    gt->up[0] = 0, gt->up[1] = 0, gt->up[2] = 1;
    if (gate_kind(gt) != 0 && fwd[0] * fwd[0] + fwd[1] * fwd[1] > 1e-4f)
        gt->up[0] = fwd[0], gt->up[1] = fwd[1], gt->up[2] = 0;
    gt->valid = 1;
    static const char *kind[] = {"ceiling", "wall", "floor"};
    vg_log("gate %c (%s) at (%.2f %.2f %.2f) normal (%.2f %.2f %.2f) ground %.2f", 'A' + S->next,
           kind[gate_kind(gt) + 1], pos[0], pos[1], pos[2], gt->n[0], gt->n[1], gt->n[2], ground);
    S->next ^= 1;
}

static CRITICAL_SECTION g_cs;  // cast and update can come from different game job threads


// ---------------------------------------------------------------- the gates' own aim
// Far Reach's target (+0x108) follows the player's Far Reach range attributes (stock 15 m across / 6.2 m up; a mod
// can make it 300 m -- gates out of the level) and snaps to ledges. The gates aim with their own ray instead:
// straight from the eye, 20 m (40 m with Far Rift), static world only.
#define AIM_RANGE 20.0f
static struct {
    int valid;          // a gate can go there
    float pos[3], n[3];
    float dist;
    const char *why;    // why not (for the log)
} g_aim;
static void aim_own_update(void);

void __declspec(dllexport) vg_module_cast(uintptr_t comp)
{
    cpnt_fn origCast = (cpnt_fn)g_host->origCast;
    uintptr_t decl = *(uintptr_t *)(comp + CPNT_DECL);
    if (!decl || *(int *)(decl + DECL_POWER_ID) != POWER_VOIDGATE) {
        origCast(comp);
        return;
    }
    // a game job too: wait a little for the lock, never indefinitely (the holder may be waiting for this job)
    int locked = 0;
    for (int k = 0; k < 50 && !(locked = TryEnterCriticalSection(&g_cs)); k++)
        Sleep(1);
    if (!locked) {
        vg_log("Void Gate cast: state busy, cast skipped");
        *(char *)(comp + GRAPPLE_TARGET_VALID) = 0;
        origCast(comp);
        return;
    }
    if (S->castThread != GetCurrentThreadId()) {
        S->castThread = GetCurrentThreadId();
        vg_log("cast thread %lu", S->castThread);
    }
    S->gateDef = *(uintptr_t *)(decl + DECL_GATE_DEF);
    aim_own_update();  // (fresh: the aim at the moment of the cast)
    char valid = (char)g_aim.valid;
    if (valid && g_surfCheck) {
        const char *why = surface_problem(g_aim.pos, g_aim.n);
        if (why)
            valid = 0, g_aim.why = why;
    }
    float *hit = g_aim.pos, *n = g_aim.n;
    vg_log("Void Gate cast: valid %d hit (%.2f %.2f %.2f) n (%.2f %.2f %.2f) %.1f m%s%s", valid, hit[0], hit[1], hit[2],
           n[0], n[1], n[2], g_aim.dist, valid ? "" : ": ", valid ? "" : (g_aim.why ? g_aim.why : "?"));
    // never Far Reach's own pull, whatever it aims at
    *(char *)(comp + GRAPPLE_TARGET_VALID) = 0;
    if (valid) {
        uintptr_t p = player();
        place_gate(hit, n, p ? ((float *)(p + ENT_ORIGIN))[2] : hit[2]);
        // Far Reach's own "no target" branch (no pull, its cleanup stays consistent), then the base power cast so
        // the cast still costs mana and starts the cooldown.
        *(char *)(comp + GRAPPLE_TARGET_VALID) = 0;
    }
    LeaveCriticalSection(&g_cs);
    // Game calls only with our lock released: spawning waits on other game jobs, and one of them (the player
    // physics update) runs our per-frame tick -- holding the lock across these calls froze the game.
    if (valid) {
        gate_entities_update();
        cleanup_flashes();
        if (*(uint32_t *)(comp + GRAPPLE_AIM_FX_HANDLE) != ~0u && *(uintptr_t *)(comp + GRAPPLE_AIM_FX)) {
            ((fx_stop_fn)FN_FX_STOP)(0, 0, 0, *(uint32_t *)(comp + GRAPPLE_AIM_FX_HANDLE));
            *(uintptr_t *)(comp + GRAPPLE_AIM_FX) = 0;
            *(uint32_t *)(comp + GRAPPLE_AIM_FX_HANDLE) = ~0u;
        }
    }
    origCast(comp);
    if (valid)
        ((cpnt_fn)FN_POWER_BASECAST)(comp);
}

// Both gates go away, the next cast opens gate A again. Their entities get the "remove" event through the game's
// event queue (what a cpntFX does with m_removeEntityOnFxEnd), so this works from any game job: the removal itself
// runs at the engine's safe point and stops the gate FX the component's own way.
static void close_gates(void)
{
    int locked = 0;
    for (int k = 0; k < 50 && !(locked = TryEnterCriticalSection(&g_cs)); k++)
        Sleep(1);
    if (!locked)
        return;
    uint32_t ent[2];
    uintptr_t fc[2];
    float pos[2][3], n[2][3];
    for (int i = 0; i < 2; i++) {
        ent[i] = S->gate[i].ent, fc[i] = S->gate[i].fxComp;
        memcpy(pos[i], S->gate[i].pos, 12), memcpy(n[i], S->gate[i].n, 12);
        memset(&S->gate[i], 0, sizeof S->gate[i]);
    }
    S->next = 0, S->exitGate = -1, S->stuckGate = -1, S->lastFrom = -1, S->pendingVOn = 0, S->rescueCheckAt = 0;
    LeaveCriticalSection(&g_cs);
    vg_log("gates closed");
    for (int i = 0; i < 2; i++)
        gate_fade_out(i, ent[i], fc[i], pos[i], n[i]);
}

// Test channel: gate i placed at pos with normal n, as a cast there would (the next cast places the other one).
static void place_gate_at(int i, const float pos[3], const float n[3])
{
    int locked = 0;
    for (int k = 0; k < 50 && !(locked = TryEnterCriticalSection(&g_cs)); k++)
        Sleep(1);
    if (!locked)
        return;
    uintptr_t p = player();
    S->next = i;
    place_gate(pos, n, p ? ((float *)(p + ENT_ORIGIN))[2] : pos[2]);  // (leaves S->next = i ^ 1)
    LeaveCriticalSection(&g_cs);
    gate_entities_update();
}

// A tap on INTERACT (F) closes the gates -- unless F means something else right now: an interaction prompt on screen
// (door, pickup, body...) or another "[F] ..." status prompt (cancel an aimed power, drop a body, the Timepiece).
// Both are read by the HUD hook (the HUD's own thread); holding F is the game's (it holsters the weapons).
#define VK_INTERACT 'F'
static volatile DWORD g_promptSeenAt;   // an interaction prompt was on screen at this time
static volatile int g_otherInteract;    // the status prompts have another "[F] ..." (as of their last rebuild)
static int game_has_focus(void)
{
    DWORD pid = 0;
    HWND w = GetForegroundWindow();
    return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
}

static void interact_tap(void)
{
    static int was;
    int down = (GetAsyncKeyState(VK_INTERACT) & 0x8000) != 0;
    if (down && !was && game_has_focus() && (S->gate[0].valid || S->gate[1].valid)) {
        if (GetTickCount() - g_promptSeenAt < 250 || g_otherInteract)
            vg_log("F: kept for the game (%s)", g_otherInteract ? "another [F] prompt" : "interaction prompt");
        else {
            // the escape: gates can lead where the player was never meant to go (out of the level, under it). After a
            // pass, 5 m below where the gate let the player out and away from both gates, F also takes the player
            // back to where they went in (the gates close as usual).
            uintptr_t p = player();
            float *f = p ? (float *)(p + ENT_ORIGIN) : NULL;
            if (f && g_lastPassAt && GetTickCount() - g_lastPassAt < 180000 && f[2] < g_lastExit[2] - 5.0f &&
                dist2(f, S->gate[0].pos) > 4.0f * 4.0f && dist2(f, S->gate[1].pos) > 4.0f * 4.0f) {
                vg_log("F: lost after the gates (z %.2f, exit %.2f): back to (%.2f %.2f %.2f)", f[2], g_lastExit[2],
                       g_lastEntry[0], g_lastEntry[1], g_lastEntry[2]);
                post_setviewpos(*(uint32_t *)(p + ENT_ID), g_lastEntry, g_lastEntryYaw, 0);
                g_lastPassAt = 0;
            }
            close_gates();
        }
    }
    was = down;
}

// ---------------------------------------------------------------- HUD prompt "[F] Close the gates"
// The status line under the crosshair (arkHUDElementStatusInformation, update 0x140529260) rebuilds its prompt list
// when flag 8 of +0x662 is set, then pushes it to the UI (0x14052ABE0). Each prompt = {localized string id, idStr
// action name}; the host hooks the update (phase 0: ask for a rebuild when ours comes or goes) and the push (phase 1:
// add ours to the rebuilt list).
#define HUD_STATUS_FLAGS 0x662
#define HUD_STATUS_LIST 0x678
#define FN_HUD_STATUS_FIND 0x14052BA80ull  // (list, prompt*) -> index or -1
#define FN_HUD_STATUS_ADD 0x14052B8E0ull   // (list, prompt*)
#define LANG_ENTRIES 0x143577E60ull        // idLangDict::langEntry_t[] (0x58: id, key idStr +8, text idStr +0x30)
#define LANG_COUNT 0x143577E68ull          // their count in the low 24 bits
#define LABEL_CLOSE_GATES "ui_statusinteraction_closevoidgate"
typedef struct {
    int label;
    int pad;
    const char *data;  // idStr: data, length (low 29 bits), capacity (bit 30 = heap), inline buffer
    uint32_t len, alloced;
    char buf[24];
} hud_prompt_t;

static int lang_entry_is(uintptr_t e, const char *key, uint32_t kl)
{
    const char *k = *(const char **)(e + 8);
    return k && (*(uint32_t *)(e + 0x10) & 0x1fffffff) == kl && !memcmp(k, key, kl + 1);
}

static int lang_id(const char *key, int *id)
{
    static int cachedIdx = -1;  // the table is rebuilt with the language: check, then search again
    static DWORD missedAt;
    uintptr_t t = *(uintptr_t *)LANG_ENTRIES;
    int n = *(uint32_t *)LANG_COUNT & 0xffffff;
    uint32_t kl = (uint32_t)strlen(key);
    if (!t || n <= 0)
        return 0;
    if (cachedIdx < 0 || cachedIdx >= n || !lang_entry_is(t + (uintptr_t)cachedIdx * 0x58, key, kl)) {
        cachedIdx = -1;
        if (missedAt && GetTickCount() - missedAt < 5000)
            return 0;  // (a full scan per frame would be wasted)
        for (int i = 0; i < n && cachedIdx < 0; i++)
            if (lang_entry_is(t + (uintptr_t)i * 0x58, key, kl))
                cachedIdx = i;
        if (cachedIdx < 0) {
            missedAt = GetTickCount();
            return 0;
        }
    }
    *id = *(int *)(t + (uintptr_t)cachedIdx * 0x58);
    return 1;
}

// The interaction prompts ("[F] Open"...): arkHUDElementInteractives, a sibling of the status element in the HUD
// (found by its vtable near the status element's owner). Its list +0x80 (count +0x88) holds 0x40-byte prompts with
// flags at +0x3A: 4 shown, 8 going away.
#define VT_HUD_INTERACTIVES 0x141E74F58ull
static uintptr_t hud_interactives(uintptr_t statusElem)
{
    static uintptr_t found;
    static DWORD failedAt;
    if (found && readable(found, 0x90) && *(uintptr_t *)found == VT_HUD_INTERACTIVES)
        return found;
    found = 0;
    if (failedAt && GetTickCount() - failedAt < 5000)
        return 0;
    uintptr_t hud = *(uintptr_t *)(statusElem + 8);
    if (readable(hud, 0x2000))
        for (int off = 0; off < 0x2000 && !found; off += 8) {
            uintptr_t p = *(uintptr_t *)(hud + off);
            if (p < 0x10000 || (p & 7) || !readable(p, 0x200))
                continue;
            if (*(uintptr_t *)p == VT_HUD_INTERACTIVES)
                found = p;
            else  // an array of element pointers
                for (int k = 0; k < 0x200 && !found; k += 8) {
                    uintptr_t q = *(uintptr_t *)(p + k);
                    if (q >= 0x10000 && !(q & 7) && readable(q, 0x90) && *(uintptr_t *)q == VT_HUD_INTERACTIVES)
                        found = q;
                }
        }
    failedAt = found ? 0 : GetTickCount();
    vg_log("HUD interaction prompts element: %p", (void *)found);
    return found;
}

static int interaction_prompt_shown(uintptr_t statusElem)
{
    uintptr_t h = hud_interactives(statusElem);
    if (!h)
        return 0;
    uintptr_t list = *(uintptr_t *)(h + 0x80);
    int n = *(uint32_t *)(h + 0x88) & 0xffffff;
    if (!list || n <= 0 || n > 64 || !readable(list, (size_t)n * 0x40))
        return 0;
    for (int i = 0; i < n; i++) {
        uint8_t f = *(uint8_t *)(list + (uintptr_t)i * 0x40 + 0x3A);
        if ((f & 4) && !(f & 8))
            return 1;
    }
    return 0;
}

void __declspec(dllexport) vg_module_hud(uintptr_t elem, int phase)
{
    static const char action[] = "<GBA_INTERACT>";
    static int shown = -1, lastPrompt, logs;
    static DWORD warned;
    int id = 0, want = g_hudPrompt && (S->gate[0].valid || S->gate[1].valid) && player();
    if (want && !lang_id(LABEL_CLOSE_GATES, &id)) {
        if (!warned)
            vg_log("HUD: no string %s", LABEL_CLOSE_GATES);
        warned = 1;
        want = 0;
    }
    if (phase == 0) {
        int prompt = want && interaction_prompt_shown(elem);
        if (prompt)
            g_promptSeenAt = GetTickCount();
        if (prompt != lastPrompt && logs++ < 40)  // (first ones only: confirms what the flags mean)
            vg_log("HUD: interaction prompt %s", prompt ? "shown" : "gone");
        lastPrompt = prompt;
        // one "[F]" at a time: ours steps aside for an interaction prompt or another "[F]" status prompt (the
        // game rebuilds the list itself when that one goes away)
        want = want && !prompt && !g_otherInteract;
        if (want != shown)
            *(uint8_t *)(elem + HUD_STATUS_FLAGS) |= 8;
        return;
    }
    // the game's own prompts are in the list now: another "[F] ..." keeps F for the game
    uintptr_t list = elem + HUD_STATUS_LIST, data = *(uintptr_t *)list;
    int n = *(uint32_t *)(list + 8) & 0xffffff, other = 0;
    for (int i = 0; data && i < n; i++) {
        hud_prompt_t *q = (hud_prompt_t *)(data + (uintptr_t)i * 0x30);
        if (q->label != id && q->data && (q->len & 0x1fffffff) == sizeof action - 1 &&
            !memcmp(q->data, action, sizeof action))
            other = 1;
    }
    g_otherInteract = other;
    want = want && !other && !lastPrompt;
    shown = want;
    if (!want)
        return;
    hud_prompt_t p = {.label = id, .data = action, .len = sizeof action - 1, .alloced = sizeof action};
    if (((int (*)(uintptr_t, hud_prompt_t *))FN_HUD_STATUS_FIND)(list, &p) < 0)
        ((int (*)(uintptr_t, hud_prompt_t *))FN_HUD_STATUS_ADD)(list, &p);
}

// ---------------------------------------------------------------- debug commands (dmod_cmd.txt in the game folder)
static void log_pos(void)
{
    uintptr_t p = player();
    float eye[3], fwd[3];
    if (!p || !player_view(eye, fwd)) {
        vg_log("pos: no player");
        return;
    }
    float *o = (float *)(p + ENT_ORIGIN);
    vg_log("pos: feet (%.3f %.3f %.3f) eye (%.3f %.3f %.3f) fwd (%.2f %.2f %.2f)", o[0], o[1], o[2], eye[0], eye[1],
           eye[2], fwd[0], fwd[1], fwd[2]);
}

// ---------------------------------------------------------------- test channel (tools/d2ctl.py)
// Requests: dmod_cmd.txt in the game folder, lines "<id> <command> [args]" (written to .tmp, then renamed).
// Replies: appended to dmod_reply.txt as "<id> <json>". The window thread picks the requests up (also in menus) and
// answers what needs no game call: reads, and console text (appended to the engine's command buffer, which the
// same thread executes in its frame). Commands that change the world wait for the next game frame
// (vg_module_frame) and run there, outside our lock.
#define CMD_SYSTEM 0x1424F6920ull           // idCmdSystemLocal (static); +8 = registered commands {next, name, fn, help}
#define CMD_APPEND_SLOT 9                   // AppendCommandText(text): run at the engine's next command buffer pass
typedef struct {
    char *p, *end;
} jbuf;
static void jcat(jbuf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b->p, b->end - b->p, fmt, ap);
    va_end(ap);
    if (n > 0)
        b->p += n < b->end - b->p ? n : b->end - b->p - 1;
}
static void jvec(jbuf *b, const char *name, const float v[3])
{
    jcat(b, "\"%s\":[%.3f,%.3f,%.3f],", name, v[0], v[1], v[2]);
}

static CRITICAL_SECTION g_replyCs;
static void reply(int id, const char *json)
{
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%sdmod_reply.txt", g_host->dir);
    EnterCriticalSection(&g_replyCs);  // (file only: no game calls under it)
    FILE *f = fopen(path, "a");
    if (f) {
        fprintf(f, "%d %s\n", id, json);
        fclose(f);
    }
    LeaveCriticalSection(&g_replyCs);
}

static volatile LONG g_frames;  // view submits (test channel "frames": frame rate)
static int g_viewOn = 0, g_viewArmed;  // see-through rendering wanted ("viewon"; off: user found it poor), armed
static int g_viewClip = 1;         // oblique near plane at the exit gate ("viewclip")
static int g_viewRect = 1;         // render only the window's part of the screen ("viewrect")
static float g_viewClipOff = 0.16f;  // the cut, in front of the exit surface ("viewclipoff")
static int g_viewUmbra = 0;        // exit plane for Umbra culling: 0 off, 1 / -1 its sign ("viewumbra")
static float g_viewOut = 0.02f;     // the window, in front of the gate entity's FX plane ("viewout")
static float g_viewFit = 0.93f;     // the polygon's corners, as a fraction of the oval ("viewfit")
static volatile uintptr_t g_aimComp;  // the Void Gate's grapple component (seen in its update)
static volatile DWORD g_aimLogUntil;  // test channel "aimlog": per-frame aim marker log until then
static float *volatile g_vzPtr;  // the player's physics velocity z (vector at -2), refreshed in the game frame
static uintptr_t g_fallState;  // last arkPlayerStateFalling seen updating for the player (tests read its height)
static DWORD g_fallStateAt;

static void cmd_state(int id)
{
    char out[4096];
    jbuf b = {out, out + sizeof out};
    uintptr_t p = player();
    float eye[3] = {0}, fwd[3] = {0};
    jcat(&b, "{\"ok\":1,\"player\":%d,", p != 0);
    if (p && player_view(eye, fwd)) {
        jvec(&b, "feet", (float *)(p + ENT_ORIGIN));
        jvec(&b, "eye", eye);
        jvec(&b, "fwd", fwd);
    }
    jcat(&b, "\"vz\":%.3f,\"safeLanding\":%d,\"safeFor\":%ld,", S->vzEst, S->safeLanding,
         (long)(S->safeUntil - GetTickCount()));
    float *pv = g_vzPtr;  // (looked up in the game frame: no game calls from this thread)
    if (p && pv && readable((uintptr_t)(pv - 2), 12))
        jvec(&b, "vel", pv - 2);
    if (g_fallState && GetTickCount() - g_fallStateAt < 500)
        jcat(&b, "\"fallHeight\":%.3f,", *(float *)(g_fallState + 0x88));
    jcat(&b, "\"exitGate\":%d,\"stuckGate\":%d,\"next\":%d,\"gates\":[", S->exitGate, S->stuckGate, S->next);
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i];
        jcat(&b, "%s{\"valid\":%d,", i ? "," : "", g->valid);
        jvec(&b, "pos", g->pos);
        jvec(&b, "n", g->n);
        jcat(&b, "\"ent\":\"%08X\",\"entAlive\":%d}", g->ent, entity_by_id(g->ent) != 0);
    }
    jcat(&b, "]}");
    reply(id, out);
}

static void cmd_cmds(int id)  // the engine's registered console commands
{
    static char out[60000];
    jbuf b = {out, out + sizeof out};
    jcat(&b, "{\"ok\":1,\"cmds\":[");
    int n = 0;
    for (uintptr_t c = *(uintptr_t *)(CMD_SYSTEM + 8); c && readable(c, 0x20) && n < 3000; c = *(uintptr_t *)c, n++) {
        const char *name = *(const char **)(c + 8);
        if (name && readable((uintptr_t)name, 2))
            jcat(&b, "%s\"%.60s\"", n ? "," : "", name);
    }
    jcat(&b, "]}");
    reply(id, out);
}

static void cmd_npcs(int id, float radius)
{
    static char out[16000];
    jbuf b = {out, out + sizeof out};
    uintptr_t g = game(), p = player();
    jcat(&b, "{\"ok\":1,\"npcs\":[");
    int n = 0;
    for (int k = 0; g && p && k < 0x4000 && n < 60; k++) {
        uintptr_t e = ((uintptr_t *)(g + OFF_ENTITIES))[k];
        if (k >= MAX_ENTITIES || !e || e == p || !readable(e + 0x360, 8) || entity_class(e) != 1888)
            continue;
        float *o = (float *)(e + ENT_ORIGIN);
        if (dist2(o, (float *)(p + ENT_ORIGIN)) > radius * radius)
            continue;
        uintptr_t r = *(uintptr_t *)(e + 0x358);
        jcat(&b, "%s{\"id\":\"%08X\",", n++ ? "," : "", *(uint32_t *)(e + ENT_ID));
        jvec(&b, "pos", o);
        const char *dn = def_name(e), *sl = strrchr(dn, '/');
        jcat(&b, "\"def\":\"%.40s\",", sl ? sl + 1 : dn);
        jcat(&b, "\"d0\":\"%08X\",\"d4\":\"%08X\",\"render\":%d,\"r1c\":\"%08X\",\"r408\":\"%08X\"}",
             *(uint32_t *)(e + 0xD0), *(uint32_t *)(e + 0xD4), r != 0,
             r && readable(r + 0x1C, 4) ? *(uint32_t *)(r + 0x1C) : 0,
             r && readable(r + 0x408, 4) ? *(uint32_t *)(r + 0x408) : 0);
    }
    jcat(&b, "]}");
    reply(id, out);
}

// world commands waiting for the game frame
#define MAX_PENDING 16
static struct {
    int id;
    char text[240];
} g_pending[MAX_PENDING];
static int g_pendingN;
static CRITICAL_SECTION g_pendingCs;

static void close_gates(void);
static void place_gate_at(int i, const float pos[3], const float n[3]);

static void run_world_command(int id, const char *line)
{
    float v[7] = {0};
    char json[256];
    int i;
    if (sscanf(line, "tp %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]) >= 3) {
        teleport_player(v, v[3], v[4]);
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "gate %d %f %f %f %f %f %f", &i, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 7 &&
               (i == 0 || i == 1)) {
        place_gate_at(i, v, v + 3);
        snprintf(json, sizeof json, "{\"ok\":1,\"ent\":\"%08X\"}", S->gate[i].ent);
        reply(id, json);
    } else if (!strcmp(line, "clear")) {
        close_gates();
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "vel %f %f %f", &v[0], &v[1], &v[2]) == 3) {  // the whole velocity vector (+0x48)
        float *vz = player_vz_ptr();
        if (vz)
            vz[-2] = v[0], vz[-1] = v[1], vz[0] = v[2];
        reply(id, vz ? "{\"ok\":1}" : "{\"ok\":0,\"error\":\"no physics state\"}");
    } else if (sscanf(line, "vz %f", &v[0]) == 1) {
        float *vz = player_vz_ptr();
        if (vz)
            *vz = v[0];
        reply(id, vz ? "{\"ok\":1}" : "{\"ok\":0,\"error\":\"no physics state\"}");
    } else if (!strncmp(line, "comp ", 5)) {  // comp <type descriptor hex> [bytes]: a player component, as floats
        uintptr_t type = (uintptr_t)strtoull(line + 5, NULL, 16), p = player();
        int bytes = 0;
        sscanf(line + 5, "%*s %d", &bytes);
        bytes = bytes > 0 && bytes <= 0x800 ? bytes : 0x160;
        uintptr_t c = p && type ? ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(p, type, 2) : 0;
        static char out[0x800 * 6];
        jbuf b = {out, out + sizeof out};
        jcat(&b, "{\"ok\":%d,\"comp\":\"%p\",\"vt\":\"%p\",\"f\":[", c != 0, (void *)c, c ? *(void **)c : 0);
        for (int k = 0; c && k < bytes && readable(c + k, 4); k += 4) {
            float f = *(float *)(c + k);
            jcat(&b, "%s%.4g", k ? "," : "", isfinite(f) && fabsf(f) < 1e7f ? f : 0.0f);
        }
        jcat(&b, "]}");
        reply(id, out);
    } else if (sscanf(line, "spawndef %f %f %f %f %f %f %255s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], json) == 7) {
        // any entityDef at a point, facing a direction (owner: the player)
        uintptr_t def = find_decl("entityDef", json);
        float axis[9];
        ((dir_to_axis_fn)FN_DIR_TO_AXIS)(v + 3, axis);
        uintptr_t e = def ? ((spawn_fn)FN_SPAWN)(game(), def, v, axis, player(), 0, 0, 0, 0) : 0;
        snprintf(json, sizeof json, "{\"ok\":%d,\"def\":\"%p\",\"ent\":\"%08X\"}", e != 0, (void *)def,
                 e ? *(uint32_t *)(e + ENT_ID) : 0);
        reply(id, json);
    } else if (!strcmp(line, "frames")) {  // view submits so far, and the time (frame rate = their deltas)
        LARGE_INTEGER t, f;
        QueryPerformanceCounter(&t), QueryPerformanceFrequency(&f);
        snprintf(json, sizeof json, "{\"ok\":1,\"frames\":%ld,\"t\":%.4f}", (long)g_frames,
                 (double)t.QuadPart / f.QuadPart);
        reply(id, json);
    } else if (sscanf(line, "hudprompt %d", &i) == 1) {
        g_hudPrompt = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "upg %d", &i) == 1) {  // upgrades forced: bit 0 far, 1 through, 2 crossfire; -1 = bought
        g_upgForce = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "setqty %d %255s", &i, json) == 2) {  // test: an inventory quantity (memory only)
        uintptr_t p = player();
        uintptr_t inv = p ? ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(p, TYPE_INVENTORY, 2) : 0;
        uintptr_t decl = find_decl("inventoryitem", json);
        int idx = inv && decl && item_quantity(p, json) >= 0 ? ((int (*)(uintptr_t, uintptr_t))FN_INV_INDEX)(inv, decl) : -1;
        uintptr_t arr = idx >= 0 ? *(uintptr_t *)(inv + 0x38) : 0;
        if (arr)
            *(int *)(arr + (uintptr_t)idx * 0x38 + 0x20) = i;
        reply(id, arr ? "{\"ok\":1}" : "{\"ok\":0}");
    } else if (sscanf(line, "item %255s", json) == 1) {  // quantity of an inventory item the player has
        int q = item_quantity(player(), json);
        snprintf(json, sizeof json, "{\"ok\":1,\"quantity\":%d,\"upg\":[%d,%d,%d]}", q, g_upg[0], g_upg[1], g_upg[2]);
        reply(id, json);
    } else if (!strcmp(line, "aim")) {  // the gates' own aim now
        aim_own_update();
        snprintf(json, sizeof json, "{\"ok\":1,\"valid\":%d,\"pos\":[%.2f,%.2f,%.2f],\"dist\":%.2f,\"why\":\"%s\"}",
                 g_aim.valid, g_aim.pos[0], g_aim.pos[1], g_aim.pos[2], g_aim.dist, g_aim.why ? g_aim.why : "");
        reply(id, json);
    } else if (sscanf(line, "entrylog %d", &i) == 1) {
        g_entryLog = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "newents %d", &i) == 1) {  // log entities appearing near the player for i ms
        g_newEntsUntil = GetTickCount() + (DWORD)i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewfit %f", &v[0]) == 1) {  // window polygon size in the oval; respawned
        g_viewFit = v[0];
        g_win[0].gateEnt = g_win[1].gateEnt = 0;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewout %f", &v[0]) == 1) {  // window offset; respawned at the next frame
        g_viewOut = v[0];
        g_win[0].gateEnt = g_win[1].gateEnt = 0;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewumbra %d", &i) == 1) {
        g_viewUmbra = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewrect %d", &i) == 1) {
        g_viewRect = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewclipoff %f", &v[0]) == 1) {
        g_viewClipOff = v[0];
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewclip %d", &i) == 1) {  // the exit-plane cut on / off
        g_viewClip = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "viewon %d", &i) == 1) {  // see-through rendering on / off
        g_viewOn = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "rm %x", &i) == 1) {  // remove an entity by handle (queued "remove" event)
        uintptr_t e = entity_by_id((uint32_t)i);
        if (e)
            ((char (*)(uintptr_t, uintptr_t, int, int))FN_ENTITY_POST_EVENT)(e, EV_REMOVE, 0, 0);
        reply(id, e ? "{\"ok\":1}" : "{\"ok\":0,\"error\":\"no such entity\"}");
    } else if (sscanf(line, "aimlog %f", &v[0]) == 1) {  // per-frame log of the aim marker for v seconds
        g_aimLogUntil = GetTickCount() + (DWORD)(v[0] * 1000);
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "entp %x %f %f %f", &i, &v[0], &v[1], &v[2]) == 4) {  // move an entity (Teleport_r)
        uintptr_t e = entity_by_id((uint32_t)i);
        float axis[9];
        if (e) {
            memcpy(axis, (void *)(e + ENT_AXIS), 36);
            ((teleport_fn)FN_TELEPORT)(e, v, axis, 0, 0);
            *(unsigned char *)(e + 0xD4) |= 4;
        }
        reply(id, e ? "{\"ok\":1}" : "{\"ok\":0,\"error\":\"no such entity\"}");
    } else if (sscanf(line, "ent %x", &i) == 1) {  // one entity: class, position, velocity
        uintptr_t e = entity_by_id((uint32_t)i);
        if (e) {
            float *o = (float *)(e + ENT_ORIGIN), *vel = (float *)(e + ENT_LIN_VEL);
            snprintf(json, sizeof json, "{\"ok\":1,\"cls\":%d,\"pos\":[%.2f,%.2f,%.2f],\"v\":[%.1f,%.1f,%.1f]}",
                     entity_class(e), o[0], o[1], o[2], vel[0], vel[1], vel[2]);
            reply(id, json);
        } else
            reply(id, "{\"ok\":0}");
    } else if (sscanf(line, "ents %f", &v[0]) == 1) {  // every entity within v m: id, class, position, velocity
        static char out[24000];
        jbuf b = {out, out + sizeof out};
        uintptr_t g = game(), p = player();
        jcat(&b, "{\"ok\":1,\"ents\":[");
        int n = 0;
        for (int k = 1; g && p && k < MAX_ENTITIES && n < 120; k++) {
            uintptr_t e = ((uintptr_t *)(g + OFF_ENTITIES))[k];
            if (!e || e == p || !readable(e + 0x360, 8))
                continue;
            float *o = (float *)(e + ENT_ORIGIN), *vel = (float *)(e + ENT_LIN_VEL);
            if (dist2(o, (float *)(p + ENT_ORIGIN)) > v[0] * v[0])
                continue;
            jcat(&b, "%s{\"id\":\"%08X\",\"cls\":%d,", n++ ? "," : "", *(uint32_t *)(e + ENT_ID), entity_class(e));
            jvec(&b, "pos", o);
            jcat(&b, "\"v\":[%.2f,%.2f,%.2f]}", vel[0], vel[1], vel[2]);
        }
        jcat(&b, "]}");
        reply(id, out);
    } else if (sscanf(line, "surfcheck %d", &i) == 1) {
        g_surfCheck = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "surfaceat %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        const char *why = surface_problem(v, v + 3);  // (from the game frame: as the power's own update)
        snprintf(json, sizeof json, "{\"ok\":1,\"surface\":\"%s\"}", why ? why : "ok");
        reply(id, json);
    } else if (sscanf(line, "ray %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        // distance to the first hit along a direction (up to 100 m), and to the first solid static world
        float d[3] = {v[3], v[4], v[5]}, l = sqrtf(dot3(d, d));
        for (int k = 0; k < 3 && l > 0; k++)
            d[k] /= l;
        snprintf(json, sizeof json, "{\"ok\":1,\"any\":%.2f,\"static\":%.2f}", ray_clear(v, d, 100.0f),
                 ray_clear_static(v, d, 100.0f));
        reply(id, json);
    } else if (sscanf(line, "npcsoft %d", &i) == 1) {  // soft landing for NPCs out of the gates
        g_npcSoft = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "npcfall %d", &i) == 1) {
        g_npcFall = i;
        reply(id, "{\"ok\":1}");
    } else if (sscanf(line, "npcmode %d", &i) == 1) {  // 0 = setviewpos event, 1 = Teleport_r, 2 = teleport event
        S->npcDirect = i;
        reply(id, "{\"ok\":1}");
    } else
        reply(id, "{\"ok\":0,\"error\":\"unknown command\"}");
}

static void run_pending(void)  // game frame
{
    int n;
    int ids[MAX_PENDING];
    char text[MAX_PENDING][240];
    EnterCriticalSection(&g_pendingCs);
    n = g_pendingN;
    for (int k = 0; k < n; k++)
        ids[k] = g_pending[k].id, memcpy(text[k], g_pending[k].text, sizeof text[k]);
    g_pendingN = 0;
    LeaveCriticalSection(&g_pendingCs);
    for (int k = 0; k < n; k++) {
        vg_log("cmd %d (game frame): %s", ids[k], text[k]);
        run_world_command(ids[k], text[k]);
    }
}

// The test channel is for development only: it runs only when a file named dmod_dev.txt exists in the game folder
// (checked once per module load), so a player's game never takes commands from a file.
static int g_devChannel = -1;

static void run_commands(void)  // window thread
{
    static DWORD last;
    if (g_devChannel < 0) {
        char marker[MAX_PATH];
        snprintf(marker, sizeof marker, "%sdmod_dev.txt", g_host->dir);
        g_devChannel = GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES;
        vg_log("test channel %s", g_devChannel ? "on (dmod_dev.txt is in the game folder)" : "off");
    }
    if (!g_devChannel || GetTickCount() - last < 50)
        return;
    last = GetTickCount();
    char path[MAX_PATH], buf[4096];
    snprintf(path, sizeof path, "%sdmod_cmd.txt", g_host->dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    DeleteFileA(path);
    buf[n] = 0;
    char *save = NULL;
    for (char *line = strtok_s(buf, "\r\n", &save); line; line = strtok_s(NULL, "\r\n", &save)) {
        int id = 0, used = 0;
        if (sscanf(line, "%d %n", &id, &used) < 1)
            continue;
        const char *c = line + used;
        float r;
        if (!strcmp(c, "ping")) {
            char json[128];
            snprintf(json, sizeof json, "{\"ok\":1,\"player\":%d,\"playerPtr\":\"%p\",\"updates\":%lu}", player() != 0,
                     (void *)player(), S->updates);
            reply(id, json);
        } else if (!strcmp(c, "state"))
            cmd_state(id);
        else if (!strcmp(c, "cmds"))
            cmd_cmds(id);
        else if (sscanf(c, "npcs %f", &r) == 1)
            cmd_npcs(id, r);
        else if (!strncmp(c, "log ", 4)) {
            vg_log("== %s", c + 4);
            reply(id, "{\"ok\":1}");
        } else if (!strncmp(c, "console ", 8)) {
            vg_log("cmd %d: console \"%s\"", id, c + 8);
            uintptr_t vt = *(uintptr_t *)CMD_SYSTEM;
            ((void (*)(uintptr_t, const char *))((uintptr_t *)vt)[CMD_APPEND_SLOT])(CMD_SYSTEM, c + 8);
            ((void (*)(uintptr_t, const char *))((uintptr_t *)vt)[CMD_APPEND_SLOT])(CMD_SYSTEM, "\n");
            reply(id, "{\"ok\":1}");
        } else {
            EnterCriticalSection(&g_pendingCs);
            if (g_pendingN < MAX_PENDING) {
                g_pending[g_pendingN].id = id;
                lstrcpynA(g_pending[g_pendingN].text, c, sizeof g_pending[0].text);
                g_pendingN++;
                used = -1;
            }
            LeaveCriticalSection(&g_pendingCs);
            if (used != -1)
                reply(id, "{\"ok\":0,\"error\":\"queue full\"}");
        }
    }
}

// ---------------------------------------------------------------- fall protection
// The player's falling state (arkPlayerStateFalling, update 0x1409E2950) adds up the drop every frame
// (+0x88 m_accumulatedFallHeight += last z (+0x94) - z) and deals fall damage from it on landing (8 m hurts,
// 17 m kills: falling.playerstatefalling). A teleport down is a drop like any other, so a fall through a gate
// killed. While protected, the drop is zeroed right before each of its updates (host hook on its vtable): in the
// column above a floor gate while falling fast, and after coming out of a gate until the player has landed (the
// Portal rule: no fall damage from a fling). (The god cvar ark_god_player is read by nothing in the retail build.)
#define FALL_STATE_OWNER 0x10               // -> context whose +0x10 is the player entity
#define FALL_ACCUM 0x88                     // m_accumulatedFallHeight
#define FALL_LAST_ORIGIN 0x8C               // m_lastFrameOrigin
static int fall_protected(void)
{
    return S->safeLanding || (LONG)(S->safeUntil - GetTickCount()) > 0;
}

void __declspec(dllexport) vg_module_falling(uintptr_t state)
{
    uintptr_t ctx = *(uintptr_t *)(state + FALL_STATE_OWNER), p = player();
    if (!S || !p || !ctx || *(uintptr_t *)(ctx + 0x10) != p)
        return;
    g_fallState = state, g_fallStateAt = GetTickCount();
    if (!fall_protected())
        return;
    float *acc = (float *)(state + FALL_ACCUM);
    if (*acc > S->zeroedMax)
        S->zeroedMax = *acc;
    *acc = 0;
    memcpy((void *)(state + FALL_LAST_ORIGIN), (void *)(p + ENT_ORIGIN), 12);
}

static void safe_landing_start(void)
{
    if (!S->safeLanding)
        vg_log("fall protection on (until landing)");
    S->safeLanding = 1;
    S->safeLandingSince = GetTickCount();
    S->stillSince = 0;
}

static void fall_protection(const float feet[3])
{
    DWORD now = GetTickCount();
    if (S->vzEst < -3.0f)
        for (int i = 0; i < 2; i++) {
            gate_t *g = &S->gate[i];
            float dx = feet[0] - g->pos[0], dy = feet[1] - g->pos[1];
            if (g->valid && gate_kind(g) > 0 && dx * dx + dy * dy < 0.85f * 0.85f && feet[2] > g->pos[2] - 0.3f)
                S->safeUntil = now + 1500;
        }
    // landed: no vertical speed for a quarter second (not just the top of a fling), or 15 s at most
    if (S->safeLanding && now - S->safeLandingSince > 400) {
        if (fabsf(S->vzEst) > 0.5f)
            S->stillSince = 0;
        else if (!S->stillSince)
            S->stillSince = now;
        if ((S->stillSince && now - S->stillSince >= 250) || now - S->safeLandingSince > 15000) {
            S->safeLanding = 0;
            vg_log("fall protection off (landed; fall height zeroed up to %.1f m)", S->zeroedMax);
            S->zeroedMax = 0;
        }
    }
}

// "Gates sometimes invisible": log each gate's FX state whenever it changes (entity, cpntFX started flag, FX handle,
// FX instance alive)
static void gate_fx_diag(void)
{
    static DWORD last;
    static uint32_t seen[2];
    if (GetTickCount() - last < 1000)
        return;
    last = GetTickCount();
    uintptr_t fs = fx_system();
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i];
        uintptr_t e = g->valid ? entity_by_id(g->ent) : 0, fc = g->fxComp;
        uint32_t h = ~0u, started = 0, alive = 0;
        if (e && fc && readable(fc + 0x4C, 0x28) && *(uintptr_t *)(fc + 0x10) == e) {
            h = *(uint32_t *)(fc + 0x4C), started = *(uint8_t *)(fc + 0x70) | *(uint8_t *)(fc + 0x71) << 1;
            alive = h != ~0u && fs && ((fx_find_fn)FN_FX_FIND)(fs, h) != 0;
        }
        uint32_t state = !g->valid ? 0 : 1 | !!e << 1 | !!fc << 2 | started << 3 | alive << 5;
        if (state != seen[i])
            vg_log("gate %c FX state: entity %08X %s, cpntFX %p, started %u stopped %u, handle %08X, alive %u",
                   'A' + i, g->ent, e ? "ok" : "MISSING", (void *)fc, started & 1, started >> 1, h, alive);
        seen[i] = state;
    }
}

// A gate's FX can end on its own: a moment after it started (on the Dreadful Wale about every second gate), or when
// the player comes out of the other gate. The cpntFX's end listener (0x14058E7F0) then sets its FX handle to -1,
// and its update (0x14058E040) never starts the FX again ("started" +0x70 stays 1): the gate works but is invisible.
// Watchdog: a live gate whose cpntFX has started, was not stopped (+0x71) and has no FX any more gets "started"
// cleared, so the component's own update starts its FX again (a new instance with the listener attached). At most
// 8 restarts per gate entity, each logged with how long the FX had lived.
static struct {
    uint32_t ent;
    DWORD liveSince;
    int restarts;
} g_fxWatch[2];
static void gate_fx_watchdog(void)
{
    DWORD now = GetTickCount();
    for (int i = 0; i < 2; i++) {
        gate_t *g = &S->gate[i];
        if (g_fxWatch[i].ent != g->ent)
            g_fxWatch[i].ent = g->ent, g_fxWatch[i].liveSince = now, g_fxWatch[i].restarts = 0;
        uintptr_t e = g->valid ? entity_by_id(g->ent) : 0, fc = g->fxComp;
        if (!e || !fc || !readable(fc + 0x4C, 0x28) || *(uintptr_t *)(fc + 0x10) != e)
            continue;
        if (!*(uint8_t *)(fc + 0x70) || *(uint8_t *)(fc + 0x71) || *(uint32_t *)(fc + 0x4C) != ~0u ||
            g_fxWatch[i].restarts >= 8)
            continue;
        g_fxWatch[i].restarts++;
        vg_log("gate %c FX ended on its own after %.1f s: restarting it (%d)", 'A' + i,
               (now - g_fxWatch[i].liveSince) / 1000.0f, g_fxWatch[i].restarts);
        g_fxWatch[i].liveSince = now;
        *(uint8_t *)(fc + 0x70) = 0;
    }
}

// Everything that touches the world, once per frame from the Void Gate component's own update.
static void game_tick(void)
{
    uintptr_t p = player();
    if (p != S->lastPlayer) {
        if (S->lastPlayer)
            vg_log("player entity changed %p -> %p: gates cleared", (void *)S->lastPlayer, (void *)p);
        memset(S->gate, 0, sizeof S->gate);  // FX went away with the old map / player
        memset(S->passed, 0, sizeof S->passed);
        memset(S->burst, 0, sizeof S->burst);
        S->next = 0;
        S->exitGate = -1, S->stuckGate = -1, S->lastFrom = -1;
        S->lastPlayer = p;
        S->safeLanding = 0, S->safeUntil = 0;
        g_aimComp = 0;
    }
    if (!p)
        return;
    float *feet = (float *)(p + ENT_ORIGIN);
    if (S->verifyAt && GetTickCount() >= S->verifyAt) {
        S->verifyAt = 0;
        vg_log("after teleport: feet (%.2f %.2f %.2f), wanted (%.2f %.2f %.2f)", feet[0], feet[1], feet[2],
               S->verifyTo[0], S->verifyTo[1], S->verifyTo[2]);
        float dx = feet[0] - S->verifyTo[0], dy = feet[1] - S->verifyTo[1];
        if (dx * dx + dy * dy > 2.0f * 2.0f && S->lastFrom >= 0) {
            S->stuckGate = S->lastFrom;
            vg_log("  the teleport did not happen: gate %c waits until the player steps off it", 'A' + S->stuckGate);
        }
    }
    // vertical speed from the origin, smoothed over a few frames
    {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        double dt = S->lastZTime ? (double)(now.QuadPart - S->lastZTime) / freq.QuadPart : 0;
        float jump = dist2(feet, S->lastPos);  // (a teleport is no speed)
        // this runs a few times per origin update (the same position 3-4 calls in a row): only a moved origin is a
        // sample, over the time since the last one (repeats read as zero speed: a fall at 28 m/s measured -1..-10
        // and missed a floor gate)
        if (jump == 0.0f && dt < 0.1)
            goto speed_done;
        if (dt > 0.002 && dt < 0.25 && jump < 3.0f * 3.0f) {
            memcpy(g_segFrom, S->lastPos, 12), memcpy(g_segTo, feet, 12), g_segAt = GetTickCount();
            S->vzEst = 0.5f * S->vzEst + 0.5f * (float)((feet[2] - S->lastZ) / dt);
            for (int k = 0; k < 3; k++)
                S->vEst[k] = 0.5f * S->vEst[k] + 0.5f * (float)((feet[k] - S->lastPos[k]) / dt);
        } else if (dt >= 0.25 || jump >= 3.0f * 3.0f)
            S->vzEst = 0, S->vEst[0] = S->vEst[1] = S->vEst[2] = 0;
        S->lastZ = feet[2], S->lastZTime = now.QuadPart;
        memcpy(S->lastPos, feet, 12);
    speed_done:;
    }
    // fell through the world after a teleport (an exit inside geometry, or out of the level): back to where the player
    // came from. Not "10 m below the exit after 1.5 s" any more: a gate high on a wall over a deep drop is a legit
    // fall, and that rule sent the user back again and again (log 2026-10-15: 33 m drop, 7 returns in a row). Now:
    // still falling fast 4.5 s after the exit without having landed (~160 m at the game's gravity: no level drops
    // that far; fall protection keeps the fall harmless, so without this the player would fall forever).
    if (S->rescueCheckAt) {
        DWORD since = GetTickCount() - (S->rescueCheckAt - 1500);  // (armed 1.5 s after the teleport before)
        if (!S->safeLanding)
            S->rescueCheckAt = 0;  // landed: a legit fall, however deep
        else if (since > 4500 && S->vzEst < -8.0f) {
            S->rescueCheckAt = 0;
            vg_log("player fell through the world (still falling %.1f s after the exit, z %.2f, exit %.2f): back to "
                   "(%.2f %.2f %.2f)", since / 1000.0f, feet[2], S->verifyTo[2], S->rescueTo[0], S->rescueTo[1],
                   S->rescueTo[2]);
            post_setviewpos(*(uint32_t *)(p + ENT_ID), S->rescueTo, S->rescueYaw, 0);
            // back on the gate it went into: that gate waits until the player has stepped off it (no loop)
            if (S->lastFrom >= 0)
                S->stuckGate = S->lastFrom;
            S->exitGate = -1;
        }
    }
    fall_protection(feet);  // (also ends a protection with the gates gone)
    npc_soft_landing();     // (NPCs out of the gates: also after the gates are gone)
    gate_fx_watchdog();
    gate_fx_diag();
    if (!S->gate[0].valid || !S->gate[1].valid)
        return;
    scan_entities();
    check_npc_teleports();
    npc_impulses();
    // the gate just come out of stays inert until the player has stepped off it (no ping-pong between floor gates)
    if (S->exitGate >= 0) {
        gate_t *x = &S->gate[S->exitGate];
        float dx = feet[0] - x->pos[0], dy = feet[1] - x->pos[1];
        if (!touching(x, feet) && dx * dx + dy * dy > 1.0f * 1.0f && GetTickCount() >= S->noTeleportUntil)
            S->exitGate = -1, S->stuckGate = -1, S->lastFrom = -1;
    }
    apply_momentum(feet);
    float vz = S->vzEst;  // measured from the origin: the physics field read 0 in real falls
    // a teleport the engine refused (e.g. while swimming) leaves the player on the entry gate: don't retry every
    // 0.4 s, wait until the player has stepped off it
    if (S->stuckGate >= 0 && !touching(&S->gate[S->stuckGate], feet))
        S->stuckGate = -1;
    int inside = -1;
    if (g_entryLog)
        vg_log("entry loop: feet (%.2f %.2f %.2f) vz %.1f exit %d stuck %d", feet[0], feet[1], feet[2], vz, S->exitGate,
               S->stuckGate);
    for (int i = 0; i < 2; i++)
        if ((i != S->exitGate || vz < -3.0f) && i != S->stuckGate && player_entered(i, feet, vz))
            inside = i;
    if (inside < 0 || GetTickCount() < S->noTeleportUntil)
        return;
    vg_log("player went into gate %c (vz %.1f)", 'A' + inside, vz);
    player_through(inside);
    S->lastFrom = inside;
    S->exitGate = inside ^ 1;
    S->noTeleportUntil = GetTickCount() + 400;
}

// One gate step per frame, whichever hook gets there first (the power's own update runs only while it is selected,
// the gate entities' FX update runs always).
static void run_tick(void)
{
    static LARGE_INTEGER freq, last;
    LARGE_INTEGER now;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    // never wait for the lock on a game job thread: whoever holds it may be waiting for this very job
    if (!TryEnterCriticalSection(&g_cs))
        return;
    if (now.QuadPart - last.QuadPart >= freq.QuadPart / 250) {  // >= 4 ms: once per frame
        last = now;
        S->updates++;
        game_tick();
    }
    LeaveCriticalSection(&g_cs);
}

// after the player physics component's update -- every frame, whatever power is selected
void __declspec(dllexport) vg_module_frame(uintptr_t comp)
{
    (void)comp;
    if (S->gate[0].valid || S->gate[1].valid || S->safeLanding || S->lastPlayer != player())
        run_tick();
    interact_tap();  // (outside our lock: closing takes it itself)
    run_pending();   // test channel commands that change the world
    window_update(); // the see-through window follows the gates
    dying_update();  // gates that faded out
    upgrades_update();
    static DWORD velAt;
    if (GetTickCount() - velAt > 250)
        velAt = GetTickCount(), g_vzPtr = player_vz_ptr();
}

// after every arkCpntFX update: ours are the gate entities' FX
void __declspec(dllexport) vg_module_fx_update(uintptr_t comp)
{
    uintptr_t owner = *(uintptr_t *)(comp + 0x10);
    if (!owner || (!S->gate[0].ent && !S->gate[1].ent))
        return;
    uint32_t id = *(uint32_t *)(owner + ENT_ID);
    for (int i = 0; i < 2; i++)
        if (id == S->gate[i].ent)
            S->gate[i].fxComp = comp;
    if (id == S->gate[0].ent || id == S->gate[1].ent)
        run_tick();
}

// ---------------------------------------------------------------- solid-surface check (docs/d2_raycast_research.md)
// A gate needs a flat, solid piece of static world as big as its oval: not a railing, a grate, a ledge it would
// hang over, a door or a prop. Rays with hknpWorld::castRay (what Far Reach's own sensors call, from the same game
// job): the centre and 8 points at 85% of the oval must hit the same plane (+-4 cm, normals within 15 degrees),
// static world (layers 0-2) with no see-through physics material; 8 rays in the plane from the centre to the rim
// must hit nothing (a corner or an object across the oval).
#define FN_CAST_RAY 0x140E566A0ull
#define FN_LEAF_MATERIAL 0x1400C63E0ull  // (arkPhysicsWorld = game+0x60280, &u16 id, bodyId, shapeKey)
#define OFF_HK_WORLD 0x60410
#define RAY_MASK 0x20822Bu  // what Far Reach sees: static world, props, doors, NPCs (not the player)
#define RAY_REQ 0x40u
typedef struct __attribute__((aligned(16))) {
    void *codec, *filter;
    uint16_t matId, p0;
    uint32_t cfi;
    uint64_t ud;
    uint32_t lod, p1[3];
    float org[4], dir[4], inv[3];
    uint32_t signW;
    uint32_t flags, p2[3];
} ray_query_t;  // 0x70
typedef struct __attribute__((aligned(16))) {
    const void *vt;
    uint64_t ref;
    uint32_t sz, p;
    uint8_t type, p2[7];
    uint32_t mask, req;
} ray_filter_t;  // 0x28 (+ alignment)
typedef struct {
    uint32_t body;
    uint16_t mat, p;
    uint32_t key, cfi, unk, p2;
    uint64_t ud;
} body_info_t;  // 0x20
typedef struct __attribute__((aligned(16))) {
    float pos[4], nrm[4];
    float frac;
    uint32_t p;
    body_info_t q, hit;
    uint64_t tail;
} ray_hit_t;  // 0x70
typedef struct __attribute__((aligned(16))) {
    const void *vt;
    uint32_t hints, nHits;
    uint8_t b10, p[15];
    float early[4];
    ray_hit_t r;
} ray_closest_t;  // 0xA0
_Static_assert(sizeof(ray_query_t) == 0x70 && sizeof(ray_hit_t) == 0x70 && sizeof(ray_closest_t) == 0xA0, "ray");

static int cast_ray(const float o[3], const float d[3], float len, ray_hit_t *out)
{
    uintptr_t g = game(), w = g ? *(uintptr_t *)(g + OFF_HK_WORLD) : 0;
    if (!w)
        return -1;
    ray_filter_t f;
    memset(&f, 0, sizeof f);
    f.vt = (void *)0x141E349B8ull, f.sz = 0x1FFFF, f.type = 4, f.mask = RAY_MASK, f.req = RAY_REQ;
    ray_query_t q;
    memset(&q, 0, sizeof q);
    q.codec = *(void **)(w + 0x9D8), q.filter = &f, q.matId = 0xFFFF, q.lod = 2;
    for (int i = 0; i < 3; i++)
        q.org[i] = o[i], q.dir[i] = d[i], q.inv[i] = d[i] == 0.f ? 3.4028e38f : 1.f / d[i];
    q.dir[3] = len;
    q.signW = (d[0] >= 0) | (d[1] >= 0) << 1 | (d[2] >= 0) << 2 | 0x3F000000u;
    ray_closest_t c;
    memset(&c, 0, sizeof c);
    c.vt = (void *)0x141CE6AD8ull;
    for (int i = 0; i < 4; i++)
        c.early[i] = 1.8446e19f;
    c.r.frac = 3.4028e38f;
    c.r.q.body = c.r.hit.body = 0xFFFFFF, c.r.q.mat = c.r.hit.mat = 0xFFFF;
    c.r.q.key = c.r.hit.key = 0xFFFFFFFFu, c.r.q.unk = c.r.hit.unk = 0xFFFFFFFFu;
    ((void (*)(uintptr_t, const void *, void *))FN_CAST_RAY)(w, &q, &c);
    if (!c.nHits)
        return 0;
    *out = c.r;
    return 1;
}

// how far a ray goes before it hits something (len: nothing)
static float ray_clear(const float o[3], const float d[3], float len)
{
    ray_hit_t h;
    if (cast_ray(o, d, len, &h) <= 0)
        return len;
    float dd[3] = {h.pos[0] - o[0], h.pos[1] - o[1], h.pos[2] - o[2]};
    return sqrtf(dot3(dd, dd));
}

// the same, through what the wind blows away or passes (props, NPCs, grates, glass): only solid static world stops
static int hit_solid(const ray_hit_t *h);
static float ray_clear_static(const float o[3], const float d[3], float len)
{
    float from = 0;
    for (int n = 0; n < 4 && from < len; n++) {
        float p[3] = {o[0] + d[0] * from, o[1] + d[1] * from, o[2] + d[2] * from};
        ray_hit_t h;
        if (cast_ray(p, d, len - from, &h) <= 0)
            return len;
        float dd[3] = {h.pos[0] - o[0], h.pos[1] - o[1], h.pos[2] - o[2]}, at = sqrtf(dot3(dd, dd));
        if (hit_solid(&h))
            return at;
        from = at + 0.3f;  // past it
    }
    return len;
}

// the hit is static world with a solid material? (layer 0-2; physics material without the see-through flags)
static int hit_solid(const ray_hit_t *h)
{
    uintptr_t g = game(), w = g ? *(uintptr_t *)(g + OFF_HK_WORLD) : 0, bodies = w ? *(uintptr_t *)(w + 0x28) : 0;
    if (!bodies)
        return 0;
    uintptr_t body = bodies + (uintptr_t)(h->hit.body & 0xFFFFFF) * 0xB0;
    if (!readable(body + 0x6C, 4) || (*(uint32_t *)(body + 0x6C) & 0x1F) > 2)
        return 0;  // doors, movers, breakables, dynamic props, NPCs
    uint16_t mat = 0xFFFF;
    ((void *(*)(uintptr_t, uint16_t *, uint32_t, uint32_t))FN_LEAF_MATERIAL)(g + 0x60280, &mat, h->hit.body, h->hit.key);
    uintptr_t lib = *(uintptr_t *)(w + 0x988), arr = lib && readable(lib + 0x30, 8) ? *(uintptr_t *)(lib + 0x30) : 0;
    if (arr && mat != 0xFFFF && readable(arr + (uintptr_t)mat * 0x70 + 0x60, 8)) {
        uintptr_t pm = *(uintptr_t *)(arr + (uintptr_t)mat * 0x70 + 0x60);
        if (pm && readable(pm + 0x80, 1) && (*(uint8_t *)(pm + 0x80) & 0x3C))
            return 0;  // blink / grapple / windblast through (glass, blinds, grates), trigger
    }
    return 1;
}

static const char *surface_problem(const float P[3], const float nIn[3])
{
    float n[3] = {nIn[0], nIn[1], nIn[2]}, eye[3], fwd[3] = {1, 0, 0};
    vp_norm(n);
    player_view(eye, fwd);
    float up[3] = {0, 0, 1};
    if (fabsf(n[2]) > 0.7f)
        up[0] = fwd[0], up[1] = fwd[1], up[2] = 0;
    vg_frame f;
    vg_frame_make(&f, P, n, up);
    float back[3] = {-n[0], -n[1], -n[2]}, o[3];
    ray_hit_t h;
    for (int k = 0; k < 3; k++)
        o[k] = P[k] + n[k] * 0.10f;
    int r = cast_ray(o, back, 0.30f, &h);
    if (r < 0)
        return NULL;  // no physics world (loading): don't block
    if (!r || fabsf(h.frac - 0.10f) > 0.05f)
        return "no surface at the centre";
    if (!hit_solid(&h))
        return "not solid static world at the centre";
    float n0[3] = {h.nrm[0], h.nrm[1], h.nrm[2]};
    if (vp_dot(n0, n) < 0.94f)
        return "uneven at the centre";
    for (int i = 0; i < 8; i++) {
        float a = i * 0.785398f, cs = cosf(a), sn = sinf(a), rim[3], dir[3];
        for (int k = 0; k < 3; k++)
            rim[k] = P[k] + f.x[k] * GATE_HALF_H * 0.85f * sn + f.y[k] * GATE_HALF_W * 0.85f * cs;
        // the plane between the centre and the rim must be clear (a corner, a pipe, a frame across the oval);
        // 15 cm out of it: rubble, papers and uneven tiles on a floor don't count
        float dd[3] = {rim[0] - P[0], rim[1] - P[1], rim[2] - P[2]}, dl = vp_len(dd);
        for (int k = 0; k < 3; k++)
            o[k] = P[k] + n[k] * 0.15f, dir[k] = dd[k] / dl;
        if (cast_ray(o, dir, dl * 1.12f, &h) > 0)
            return "something crosses the oval";
        // and the surface must go on under the rim, flat
        for (int k = 0; k < 3; k++)
            o[k] = rim[k] + n[k] * 0.10f;
        if (cast_ray(o, back, 0.30f, &h) <= 0)
            return "the oval hangs over an edge";
        if (fabsf(h.frac - 0.10f) > 0.04f || vp_dot(h.nrm, n0) < 0.966f)
            return "the surface is not flat under the oval";
        if (!hit_solid(&h))
            return "not solid under the oval";
    }
    return NULL;
}

static void aim_own_update(void)
{
    float eye[3], fwd[3];
    g_aim.valid = 0, g_aim.why = "nothing in range";
    if (!player_view(eye, fwd))
        return;
    float range = AIM_RANGE * (HAS_UPG(UPG_FAR) ? 2.0f : 1.0f);
    ray_hit_t h;
    int r = cast_ray(eye, fwd, range, &h);
    if (r <= 0)
        return;
    float n[3] = {h.nrm[0], h.nrm[1], h.nrm[2]}, d[3] = {h.pos[0] - eye[0], h.pos[1] - eye[1], h.pos[2] - eye[2]};
    if (!vp_norm(n))
        return;
    memcpy(g_aim.pos, h.pos, 12), memcpy(g_aim.n, n, 12);
    g_aim.dist = sqrtf(dot3(d, d));
    // static world only: NPCs, props, doors, movers move (a gate on them would hang in the air)
    uintptr_t g = game(), w = g ? *(uintptr_t *)(g + OFF_HK_WORLD) : 0, bodies = w ? *(uintptr_t *)(w + 0x28) : 0;
    uintptr_t body = bodies ? bodies + (uintptr_t)(h.hit.body & 0xFFFFFF) * 0xB0 : 0;
    if (!body || !readable(body + 0x6C, 4) || (*(uint32_t *)(body + 0x6C) & 0x1F) > 2) {
        g_aim.why = "not static world";
        return;
    }
    g_aim.valid = 1, g_aim.why = NULL;
}

// the quantity of an inventory item the player has (-1: no inventory / no such item decl)
static int item_quantity(uintptr_t p, const char *name)
{
    uintptr_t inv = p ? ((uintptr_t (*)(uintptr_t, uintptr_t, int))FN_GET_COMPONENT)(p, TYPE_INVENTORY, 2) : 0;
    uintptr_t decl = find_decl("inventoryitem", name);
    if (!inv || !decl || !readable(inv + 0x38, 8))
        return -1;
    static uintptr_t checkedVt;  // the component's class, once: arkCpntInventory, or no upgrade reading at all
    uintptr_t vt = *(uintptr_t *)inv;
    if (vt != checkedVt) {
        uintptr_t col = readable(vt - 8, 8) ? *(uintptr_t *)(vt - 8) : 0;
        uintptr_t td = col && readable(col + 12, 4) ? 0x140000000ull + *(uint32_t *)(col + 12) : 0;
        const char *nm = td && readable(td + 0x10, 32) ? (const char *)(td + 0x10) : "?";
        vg_log("inventory component %p: %s", (void *)inv, nm);
        if (strcmp(nm, ".?AVarkCpntInventory@@"))
            return -1;
        checkedVt = vt;
    }
    int idx = ((int (*)(uintptr_t, uintptr_t))FN_INV_INDEX)(inv, decl);
    uintptr_t arr = *(uintptr_t *)(inv + 0x38);
    if (idx < 0 || !arr || !readable(arr + (uintptr_t)idx * 0x38 + 0x24, 4))
        return idx < 0 ? 0 : -1;
    return *(int *)(arr + (uintptr_t)idx * 0x38 + 0x20);
}

static void upgrades_update(void)  // game frame: twice a second
{
    static DWORD last;
    if (GetTickCount() - last < 500)
        return;
    last = GetTickCount();
    uintptr_t p = player();
    int now[UPG_COUNT], changed = 0;
    for (int u = 0; u < UPG_COUNT; u++) {
        int q = item_quantity(p, g_upgItem[u]);
        now[u] = q < 0 ? 1 : q > 0;  // (unreadable: everything on rather than a power that cannot do anything)
        changed |= now[u] != g_upg[u];
        g_upg[u] = now[u];
    }
    if (changed)
        vg_log("upgrades: Far Rift %d, Through the Void %d, Crossfire %d", g_upg[0], g_upg[1], g_upg[2]);
}

// per aim: the check is cached while the aimed point stays put
static int g_aimSurfaceOk = 1;
static const char *g_aimSurfaceWhy;
static void aim_surface_update(uintptr_t comp)
{
    static float lastP[3], lastN[3];
    (void)comp;
    if (!g_surfCheck || !g_aim.valid) {
        g_aimSurfaceOk = 1;
        return;
    }
    float *P = g_aim.pos, *n = g_aim.n;
    if (dist2(P, lastP) < 0.03f * 0.03f && dot3(n, lastN) > 0.999f)
        return;
    memcpy(lastP, P, 12), memcpy(lastN, n, 12);
    const char *why = surface_problem(P, n);
    g_aimSurfaceOk = !why;
    if (why != g_aimSurfaceWhy)
        vg_log("aim surface: %s", why ? why : "ok");
    g_aimSurfaceWhy = why;
}

// Far Reach puts its aim marker where it would *pull* the player (ledge top, floor under a wall...): ours goes on the
// aimed surface point, facing out of it. Called by the host in place of Far Reach's own marker move (moving it again
// after Far Reach had moved it made it jump between the two spots and orientations every frame).
void __declspec(dllexport) vg_module_aim_move(uint32_t handle, float *pos, float *axis)
{
    uintptr_t comp = g_aimComp;
    if (!comp || !readable(comp, 0x130) || *(uint32_t *)(comp + GRAPPLE_AIM_FX_HANDLE) != handle)
        return;
    uintptr_t decl = *(uintptr_t *)(comp + CPNT_DECL);
    if (!decl || *(int *)(decl + DECL_POWER_ID) != POWER_VOIDGATE)
        return;
    // our own aim (vg_module_update ran it this frame); Far Reach shows the same preview FX for every kind of target
    // (power decl), so the marker exists whatever it aims at -- hidden 50 m under the floor where no gate can go
    float up[3] = {0, 0, 1};
    ((dir_to_axis_fn)FN_DIR_TO_AXIS)(g_aim.valid ? g_aim.n : up, axis);
    for (int k = 0; k < 3; k++)
        pos[k] = g_aim.valid && g_aimSurfaceOk ? g_aim.pos[k] + g_aim.n[k] * 0.02f : (k == 2 ? -1e4f : 0);
}

// ---------------------------------------------------------------- see-through window (docs/d2_portal_render_research.md)
// The engine's one extra view (the Timepiece lens): the render world's view[1], armed each frame on the render
// system with a target image, rendered before the main view, sampled in screen space by portal.material's shader.
// Here: view[1] = the player's view carried through the gates (a viewer looking into gate A sees what a camera behind
// gate B sees), drawn into our image, shown on the window entity (test channel "spawnview", "viewon 1").
#define RENDER_SYSTEM 0x14359FB20ull       // idRenderSystemLocal: vt[0x1B8] SetPortalView, vt[0x1C0] ClearPortalView
#define FN_RV_COPY 0x140064A90ull          // renderView_t::operator=
#define FN_SCRATCH_IMAGE 0x1403751E0ull    // idImageManager::ScratchImage(-, name, opts, 0, 1)
#define FN_FMT_BITS 0x140341570ull         // (a, b, &x, format) -> bool: extra format bits for the scratch image
#define FN_PARM_REMOVE 0x1403DC040ull      // (parmBlock, parmDecl)
#define FN_PARM_SET 0x1403DBCF0ull         // (parmBlock, parmDecl, &value16)
#define PARM_PORTAL 0x14254E368ull         // [ ] = parm decl "rendering/portal"
#define RE_PARMBLOCK 0x1950                // render entity's game parm block
static uintptr_t g_portalImage[2];

static uintptr_t portal_image(int w)  // window w's target (each window keeps its last picture)
{
    if (g_portalImage[w])
        return g_portalImage[w];
    uintptr_t rs = RENDER_SYSTEM, vt = *(uintptr_t *)rs;
    uint32_t wd = ((uint32_t (*)(uintptr_t))*(uintptr_t *)(vt + 0x218))(rs);
    uint32_t ht = ((uint32_t (*)(uintptr_t))*(uintptr_t *)(vt + 0x220))(rs);
    uint32_t fmt = 0x5CB, x = 0;
    // (the 4th argument, fmt & 0x3F, is easy to miss: the Timepiece leaves it in r9 -- without it this crashed)
    if (((char (*)(uint32_t, uint32_t, uint32_t *, uint32_t))FN_FMT_BITS)((fmt >> 6) & 0xF, (fmt >> 10) & 7, &x,
                                                                          fmt & 0x3F))
        fmt = (fmt & ~0x1FE000u) | ((x << 13) & 0x1FE000u);
    uint32_t opts[0x4C / 4] = {0};  // as the Timepiece builds it (0x14050FFF0)
    opts[1] = fmt;
    opts[2] = opts[4] = opts[6] = wd;
    opts[3] = opts[5] = opts[7] = ht;
    for (int k = 0x24 / 4; k <= 0x38 / 4; k++)
        opts[k] = 1;
    opts[0x3C / 4] = *(uint32_t *)0x1424FF60Cull;
    opts[0x48 / 4] = 0x30040;
    g_portalImage[w] = ((uintptr_t (*)(uintptr_t, const char *, void *, uintptr_t, char))FN_SCRATCH_IMAGE)(
        0, w ? "_VoidGatePortalB" : "_VoidGatePortal", opts, 0, 1);
    vg_log("see-through image %c %p (%ux%u)", 'A' + w, (void *)g_portalImage[w], wd, ht);
    return g_portalImage[w];
}

// Oblique near plane (Lengyel), for the engine's reversed-Z projection (rows [.,.,.,.] row-major, column vectors,
// view space x = -left, y = up, z = -forward; clip 0 <= z <= w, near at z = w): the near plane becomes the exit
// gate's surface, so nothing between the virtual camera and gate B (the wall it is on) is drawn. The portal path
// clears "use the explicit projection" (0x140409D2F, 7 bytes): NOPed once, so the matrix below is used.
#define PATCH_PORTAL_PROJ 0x140409D2Full
static int mat4_inverse(const float m[16], float inv[16])
{
    float a[4][8];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 8; c++)
            a[r][c] = c < 4 ? m[r * 4 + c] : (c - 4 == r);
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++)
            if (fabsf(a[r][c]) > fabsf(a[p][c]))
                p = r;
        if (fabsf(a[p][c]) < 1e-12f)
            return 0;
        for (int k = 0; k < 8; k++) {
            float t = a[c][k];
            a[c][k] = a[p][k], a[p][k] = t;
        }
        float d = a[c][c];
        for (int k = 0; k < 8; k++)
            a[c][k] /= d;
        for (int r = 0; r < 4; r++)
            if (r != c) {
                float f = a[r][c];
                for (int k = 0; k < 8; k++)
                    a[r][k] -= f * a[c][k];
            }
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            inv[r * 4 + c] = a[r][c + 4];
    return 1;
}

// proj: the main projection (row-major); cam/axis: the virtual camera (axis rows forward, left, up); plane: exit
// surface n.p + d = 0, kept side n.p + d > 0. Writes the oblique matrix; 0 if not possible (camera in front).
static int oblique_projection(const float proj[16], const float cam[3], const float axis[9], const float n[3], float d,
                              float out[16])
{
    float R[9] = {-axis[3], -axis[4], -axis[5], axis[6], axis[7], axis[8], -axis[0], -axis[1], -axis[2]};
    float C[4] = {R[0] * n[0] + R[1] * n[1] + R[2] * n[2], R[3] * n[0] + R[4] * n[1] + R[5] * n[2],
                  R[6] * n[0] + R[7] * n[1] + R[8] * n[2], n[0] * cam[0] + n[1] * cam[1] + n[2] * cam[2] + d};
    if (C[3] > -0.001f)
        return 0;  // the virtual camera is in front of the exit: nothing to cut away
    float inv[16];
    if (!mat4_inverse(proj, inv))
        return 0;
    float qc[4] = {C[0] > 0 ? 1.0f : C[0] < 0 ? -1.0f : 0.0f, C[1] > 0 ? 1.0f : C[1] < 0 ? -1.0f : 0.0f, 0, 1}, Q[4];
    for (int r = 0; r < 4; r++)
        Q[r] = inv[r * 4] * qc[0] + inv[r * 4 + 1] * qc[1] + inv[r * 4 + 2] * qc[2] + inv[r * 4 + 3] * qc[3];
    float cq = C[0] * Q[0] + C[1] * Q[1] + C[2] * Q[2] + C[3] * Q[3];
    float r3q = proj[12] * Q[0] + proj[13] * Q[1] + proj[14] * Q[2] + proj[15] * Q[3];
    if (fabsf(cq) < 1e-9f)
        return 0;
    float a = r3q / cq;
    memcpy(out, proj, 64);
    for (int k = 0; k < 4; k++)
        out[8 + k] = proj[12 + k] - a * C[k];  // row 2 = row 3 - a C: near plane (row3 - row2) = a C
    return 1;
}

static int patch_portal_projection(void)
{
    static int done;
    unsigned char *p = (unsigned char *)PATCH_PORTAL_PROJ;
    static const unsigned char want[7] = {0xC6, 0x80, 0x40, 0x01, 0x00, 0x00, 0x00};
    if (done)
        return done > 0;
    if (memcmp(p, want, 7) && memcmp(p, "\x90\x90\x90\x90\x90\x90\x90", 7)) {
        vg_log("portal projection patch: unexpected bytes");
        done = -1;
        return 0;
    }
    DWORD old;
    VirtualProtect(p, 7, PAGE_EXECUTE_READWRITE, &old);
    memset(p, 0x90, 7);
    VirtualProtect(p, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 7);
    vg_log("portal projection patch applied (the second view keeps an explicit projection)");
    done = 1;
    return 1;
}

// A window on each gate (spawned and removed from the game frame: spawning from the view submit would be inside the
// render submission). The engine renders one extra view a frame: vg_module_view picks which window gets it.
static void window_remove(int w)
{
    for (int k = 0; k < WIN_PIECES; k++) {
        uintptr_t e = entity_by_id(g_win[w].ids[k]);
        if (e)
            ((char (*)(uintptr_t, uintptr_t, int, int))FN_ENTITY_POST_EVENT)(e, EV_REMOVE, 0, 0);
    }
    memset(&g_win[w], 0, sizeof g_win[w]);
}

// A save made with gates open keeps their entities (and the window): after a load they would stand there with no
// gate behind them. 2 s after the player entity changes, entities spawned from our defs that are not current go.
static void remove_leftovers(void)
{
    static uintptr_t seenPlayer;
    static DWORD changedAt;
    uintptr_t p = player(), g = game();
    if (p != seenPlayer)
        seenPlayer = p, changedAt = p ? GetTickCount() : 0;
    if (!changedAt || GetTickCount() - changedAt < 2000 || !g)
        return;
    changedAt = 0;
    static const char *names[] = {"models/powers/voidgate/voidgate_view.def", "models/powers/voidgate/voidgate_view_rect.def",
                                  "models/powers/voidgate/voidgate_gate.def", "models/powers/voidgate/voidgate_gate_b.def",
                                  "models/powers/voidgate/voidgate_pass.def"};
    uintptr_t defs[5];
    for (int k = 0; k < 5; k++)
        defs[k] = find_decl("entityDef", names[k]);
    int n = 0;
    for (int i = 1; i < MAX_ENTITIES; i++) {
        uintptr_t e = ((uintptr_t *)(g + OFF_ENTITIES))[i];
        if (!e || !readable(e + ENT_DEF, 8))
            continue;
        uintptr_t d = *(uintptr_t *)(e + ENT_DEF);
        uint32_t id = *(uint32_t *)(e + ENT_ID);
        int ours = 0;
        for (int k = 0; k < 5; k++)
            ours |= d && d == defs[k];
        if (!ours || id == S->gate[0].ent || id == S->gate[1].ent || is_window_id(id))
            continue;
        ((char (*)(uintptr_t, uintptr_t, int, int))FN_ENTITY_POST_EVENT)(e, EV_REMOVE, 0, 0);
        n++;
    }
    if (n)
        vg_log("removed %d leftover gate / window entities (from a save?)", n);
}

// the gate entity's own axis (rows: normal, side, up -- the FX lays the oval's long side along its up), so the window
// lies exactly on the rim; just in front of the gate FX's plane, so its dark middle stays behind.
// The shape: the stock rectangle (+-2.936 across, 0..3.635 up from its origin) through a sheared axis is any
// parallelogram. Piece i has the corners v_i, v_i+1, -v_i, -v_i+1 of a 2N-gon inscribed in the oval, so it holds the
// triangles (0, v_i, v_i+1) and (0, -v_i, -v_i+1): N pieces fill the whole polygon (20 sides: within 1.2% of the
// ellipse, under the rim). They all show the same screen-space image, so the overlaps don't show.
static void window_spawn(int w)
{
    uintptr_t ge = entity_by_id(S->gate[w].ent);
    uintptr_t def = find_decl("entityDef", "models/powers/voidgate/voidgate_view_rect.def");
    if (!ge || !def)
        return;
    float *ga = (float *)(ge + ENT_AXIS), *go = (float *)(ge + ENT_ORIGIN), pos[3], axis[9];
    memcpy(axis, ga, 36);
    for (int r = 0; r < WIN_PIECES; r++) {
        float a0 = 3.14159265f * r / WIN_PIECES, a1 = 3.14159265f * (r + 1) / WIN_PIECES, f = g_viewFit;
        float v0[2] = {f * cosf(a0), f * sinf(a0)}, v1[2] = {f * cosf(a1), f * sinf(a1)};  // (side, up) in half-sizes
        float e1[2] = {(v0[0] - v1[0]) / 2, (v0[1] - v1[1]) / 2};  // corner (+1,+1) = v0, (-1,+1) = v1
        float e2[2] = {(v0[0] + v1[0]) / 2, (v0[1] + v1[1]) / 2};
        float E1[3], E2[3];  // world half-axes of the piece
        for (int k = 0; k < 3; k++) {
            E1[k] = ga[3 + k] * GATE_HALF_W * e1[0] + ga[6 + k] * GATE_HALF_H * e1[1];
            E2[k] = ga[3 + k] * GATE_HALF_W * e2[0] + ga[6 + k] * GATE_HALF_H * e2[1];
            pos[k] = go[k] + ga[k] * (g_viewOut + 0.001f * r) - E2[k];  // origin: the middle of its bottom edge
        }
        uintptr_t e = ((spawn_fn)FN_SPAWN)(game(), def, pos, axis, player(), 0, 0, 0, 0);
        if (!e)
            continue;
        float *ea = (float *)(e + ENT_AXIS);  // written after the spawn (the spawn keeps the axis orthonormal)
        for (int k = 0; k < 3; k++)
            ea[3 + k] = E1[k] / 2.936f, ea[6 + k] = E2[k] * 2 / 3.635f;
        g_win[w].ids[r] = *(uint32_t *)(e + ENT_ID);
    }
    g_win[w].gateEnt = S->gate[w].ent;
    vg_log("see-through window on gate %c", 'A' + w);
}

// One live window at a time: the engine renders one extra view a frame, and two windows taking turns flickered
// (each showed a picture from another frame -- user report). It goes on the gate on screen, seen from its front, the
// player is nearer to (sticky by 2 m); with no gate on screen it stays where it is.
static int g_winVis[2];  // gate w's oval on screen, seen from its front (last frame's view submit)
static void window_update(void)  // game frame
{
    remove_leftovers();
    static DWORD lastSwitch;
    int on = g_viewOn && player() && S->gate[0].valid && S->gate[1].valid && S->gate[0].ent && S->gate[1].ent;
    int cur = g_win[0].ids[0] ? 0 : g_win[1].ids[0] ? 1 : -1, want = -1;
    if (on) {
        float eye[3], fwd[3], dist[2] = {1e9f, 1e9f};
        if (player_view(eye, fwd))
            for (int i = 0; i < 2; i++) {
                gate_t *gt = &S->gate[i];
                float d[3] = {eye[0] - gt->pos[0], eye[1] - gt->pos[1], eye[2] - gt->pos[2]};
                if (g_winVis[i] && dot3(d, gt->n) > 0.05f && dot3(d, d) < 60 * 60)
                    dist[i] = sqrtf(dot3(d, d));
            }
        want = dist[0] < dist[1] ? 0 : dist[1] < 1e9f ? 1 : -1;
        if (cur >= 0 && dist[cur] < 1e9f && (want < 0 || dist[cur] < dist[want] + 2.0f))
            want = cur;
        if (want < 0)
            want = cur;  // (nothing on screen: no respawn just for turning around)
    }
    if (want == cur && (cur < 0 || g_win[cur].gateEnt == S->gate[cur].ent))
        return;  // (a recast replaces the gate's entity: the window follows it)
    if (want >= 0 && cur >= 0 && want != cur && GetTickCount() - lastSwitch < 500)
        return;  // (no flicker between two gates at the same distance)
    if (cur >= 0)
        window_remove(cur);
    if (want >= 0)
        window_spawn(want), lastSwitch = GetTickCount();
}

// Window w on screen? Its oval's box on its gate projected with the main view's view-projection (row-major, column
// vectors, last frame's: a few pixels of margin). rect: in = the viewport, out = the box clamped to it (the whole
// viewport when a corner is behind the eye). 0: off screen, or all of it behind the eye.
static int window_on_screen(uintptr_t v0, int w, int rect[6])
{
    uintptr_t ge = entity_by_id(S->gate[w].ent);
    if (!ge)
        return 0;
    const float *vp = (const float *)(v0 + 0x7510), *o = (const float *)(ge + ENT_ORIGIN);
    const float *ax = (const float *)(ge + ENT_AXIS);  // rows: normal, side, up
    int x0 = rect[0], y0 = rect[1], x1 = rect[2], y1 = rect[3], behind = 0;
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int c = 0; c < 4; c++) {
        float p[3], cl[4];
        for (int k = 0; k < 3; k++)
            p[k] = o[k] + ax[k] * g_viewOut + ax[3 + k] * ((c & 1) ? GATE_HALF_W : -GATE_HALF_W) +
                   ax[6 + k] * ((c & 2) ? GATE_HALF_H : -GATE_HALF_H);
        for (int r = 0; r < 4; r++)
            cl[r] = vp[r * 4] * p[0] + vp[r * 4 + 1] * p[1] + vp[r * 4 + 2] * p[2] + vp[r * 4 + 3];
        if (cl[3] < 0.05f) {
            behind++;
            continue;
        }
        float px = x0 + (cl[0] / cl[3] * 0.5f + 0.5f) * (x1 - x0), py = y0 + (0.5f - cl[1] / cl[3] * 0.5f) * (y1 - y0);
        minx = px < minx ? px : minx, maxx = px > maxx ? px : maxx;
        miny = py < miny ? py : miny, maxy = py > maxy ? py : maxy;
    }
    if (behind == 4)
        return 0;
    if (behind)
        return 1;  // (partly behind the eye: standing in it -- the whole viewport)
    minx -= 24, maxx += 24, miny -= 24, maxy += 24;
    if (maxx < x0 || minx > x1 || maxy < y0 || miny > y1)
        return 0;
    rect[0] = minx > x0 ? (int)minx : x0, rect[1] = miny > y0 ? (int)miny : y0;
    rect[2] = maxx < x1 ? (int)maxx + 1 : x1, rect[3] = maxy < y1 ? (int)maxy + 1 : y1;
    return rect[2] - rect[0] >= 4 && rect[3] - rect[1] >= 4;
}

// Per frame (from the view submit): the live window gets the extra view, only while it is on screen and seen from
// its front (the back face is culled) -- an unseen window cost a whole second scene render before.
void __declspec(dllexport) vg_module_view(uintptr_t gameArg, uintptr_t frame)
{
    (void)gameArg, (void)frame;
    g_frames++;
    uintptr_t rs = RENDER_SYSTEM, rsvt = *(uintptr_t *)rs;
    int ready = g_viewOn && S->gate[0].valid && S->gate[1].valid;
    uintptr_t g = game();
    uint32_t n = g && ready ? *(uint32_t *)(g + OFF_VIEWS_N) & 0xFFFFFF : 0;
    uintptr_t list = n ? *(uintptr_t *)(g + OFF_VIEWS) : 0;
    uintptr_t rw = list ? ((uintptr_t (*)(uintptr_t))*(uintptr_t *)(*(uintptr_t *)g + 0x48))(g) : 0;
    int nv = rw ? ((int (*)(uintptr_t))*(uintptr_t *)(*(uintptr_t *)rw + 0xB0))(rw) : 0;
    if (nv < 2) {
        g_winVis[0] = g_winVis[1] = 0;
        if (g_viewArmed)
            ((void (*)(uintptr_t))*(uintptr_t *)(rsvt + 0x1C0))(rs), g_viewArmed = 0;
        return;
    }
    uintptr_t rwvt = *(uintptr_t *)rw;
    uintptr_t v1 = ((uintptr_t (*)(uintptr_t, int))*(uintptr_t *)(rwvt + 0xC0))(rw, nv - 1);
    uintptr_t v0 = ((uintptr_t (*)(uintptr_t, int))*(uintptr_t *)(rwvt + 0xC0))(rw, 0);
    uintptr_t owner = *(uintptr_t *)(list + (n - 1) * 8) - 0x20;
    float *org = (float *)(owner + 0xD8), *ax = (float *)(owner + 0xFC);  // axis rows: forward, left, up
    int vp[6] = {0}, rects[2][6];
    memcpy(vp, (void *)(v0 + 0x7878), 16);  // the main view's unscaled viewport
    if (vp[2] - vp[0] < 16 || vp[3] - vp[1] < 16)
        return;
    for (int w = 0; w < 2; w++) {
        gate_t *gt = &S->gate[w];
        float d[3] = {org[0] - gt->pos[0], org[1] - gt->pos[1], org[2] - gt->pos[2]};
        memcpy(rects[w], vp, sizeof vp);
        g_winVis[w] = dot3(d, gt->n) > 0.02f && dot3(d, d) < 100.0f * 100.0f && window_on_screen(v0, w, rects[w]);
    }
    int wi = g_win[0].ids[0] ? 0 : g_win[1].ids[0] ? 1 : -1;
    uintptr_t img = wi >= 0 && g_winVis[wi] ? portal_image(wi) : 0;
    if (!img) {
        if (g_viewArmed)
            ((void (*)(uintptr_t))*(uintptr_t *)(rsvt + 0x1C0))(rs), g_viewArmed = 0;
        return;
    }
    for (int k = 0; k < WIN_PIECES; k++) {  // the window samples its picture
        uintptr_t we = entity_by_id(g_win[wi].ids[k]), re = we ? *(uintptr_t *)(we + 0xF0) : 0;
        if (re) {
            uintptr_t pb = re + RE_PARMBLOCK, h = *(uintptr_t *)PARM_PORTAL;
            uintptr_t val[2] = {img + 0x98, 0};
            ((void (*)(uintptr_t, uintptr_t))FN_PARM_REMOVE)(pb, h);
            ((void (*)(uintptr_t, uintptr_t, uintptr_t *))FN_PARM_SET)(pb, h, val);
        }
    }
    int oi = wi ^ 1;  // the window's gate, the other one (whose side it shows)
    vg_frame fa, fb;
    gate_frame(&S->gate[wi], &fa), gate_frame(&S->gate[oi], &fb);
    float cam[3], cax[9];
    vg_map_point(&fa, &fb, org, cam);
    for (int r = 0; r < 3; r++)
        vg_map_dir(&fa, &fb, ax + 3 * r, cax + 3 * r);
    ((void (*)(uintptr_t, uintptr_t))FN_RV_COPY)(v1, owner + 0x30);
    memcpy((void *)(v1 + 0xA8), cam, 12);   // vieworg
    memcpy((void *)(v1 + 0xB4), cam, 12);   // previousvieworg
    memcpy((void *)(v1 + 0xC0), cam, 12);   // m_portalViewOrg
    memcpy((void *)(v1 + 0xCC), cax, 36);   // viewaxis
    // cut away everything behind the exit gate's surface, and its own FX (12 cm out) with it: we look through it
    float obl[16], *nb = S->gate[oi].n;
    float db = -(nb[0] * S->gate[oi].pos[0] + nb[1] * S->gate[oi].pos[1] + nb[2] * S->gate[oi].pos[2]) - g_viewClipOff;
    int oblique = 0;
    if (g_viewClip == 2 && patch_portal_projection()) {  // (debug: the main projection as is)
        memcpy((void *)(v1 + 0x100), (void *)(v0 + 0x7390), 64);
        *(uint8_t *)(v1 + 0x140) = 1;
    } else if (g_viewClip == 1 && patch_portal_projection() &&
               oblique_projection((float *)(v0 + 0x7390), cam, cax, nb, db, obl)) {
        memcpy((void *)(v1 + 0x100), obl, 64);  // explicitProjectionMatrix
        *(uint8_t *)(v1 + 0x140) = 1;            // useExplicitProjectionMatrix
        oblique = 1;
    } else
        *(uint8_t *)(v1 + 0x140) = 0;
    // only the window's part of the screen (with the oblique projection a scissor rect gave a black view: whole)
    int *rect = g_viewRect && !oblique ? rects[wi] : vp;
    // the exit plane for Umbra's occlusion culling too (a camera behind a wall otherwise culls most of the room)
    float plane[4] = {3.4028235e38f, 3.4028235e38f, 3.4028235e38f, 3.4028235e38f};
    if (g_viewUmbra) {
        float sg = g_viewUmbra > 0 ? 1.0f : -1.0f;
        plane[0] = sg * nb[0], plane[1] = sg * nb[1], plane[2] = sg * nb[2], plane[3] = sg * db;
    }
    ((void (*)(uintptr_t, uintptr_t, uintptr_t, int *, float *))*(uintptr_t *)(rsvt + 0x1B8))(rs, img, v1, rect,
                                                                                              plane);
    if (!g_viewArmed)
        vg_log("see-through: window %c drawn (on screen: A %d, B %d), rect %d %d %d %d", 'A' + wi, g_winVis[0],
               g_winVis[1], rect[0], rect[1], rect[2], rect[3]);
    g_viewArmed = 1;
}

void __declspec(dllexport) vg_module_update(uintptr_t comp)
{
    uintptr_t decl = *(uintptr_t *)(comp + CPNT_DECL);
    if (!decl || *(int *)(decl + DECL_POWER_ID) != POWER_VOIDGATE)
        return;
    if (!S->updateThread) {
        S->updateThread = GetCurrentThreadId();
        vg_log("first Void Gate update (thread %lu)", S->updateThread);
    }
    // Far Reach puts its aim marker where it would *pull* the player (ledge top, floor under a wall...): put our gate
    // preview back on the aimed surface point, facing out of it, right after Far Reach has moved it this frame
    uint32_t h = *(uint32_t *)(comp + GRAPPLE_AIM_FX_HANDLE);
    if ((LONG)(g_aimLogUntil - GetTickCount()) > 0) {
        float *hit = (float *)(comp + GRAPPLE_HIT_POS), *n = (float *)(comp + GRAPPLE_HIT_NORMAL);
        float *st = (float *)(comp + GRAPPLE_STAND_POS);
        vg_log("aim: valid %d mantle %d fx %p handle %08X hit (%.2f %.2f %.2f) n (%.2f %.2f %.2f) stand (%.2f %.2f %.2f)",
               *(char *)(comp + GRAPPLE_TARGET_VALID), *(char *)(comp + 0xD8), *(void **)(comp + GRAPPLE_AIM_FX), h,
               hit[0], hit[1], hit[2], n[0], n[1], n[2], st[0], st[1], st[2]);
    }
    g_aimComp = comp;  // (the marker itself is placed in vg_module_aim_move, inside Far Reach's own move)
    aim_own_update();
    aim_surface_update(comp);
    cleanup_flashes();  // (same context as the cast, where gate entities are removed safely)
    run_tick();
}

// Window message thread: no game calls from here; only notices when the gate logic stops getting updates.
void __declspec(dllexport) vg_module_tick(void)
{
    static DWORD lastSeen, lastCount, warned;
    DWORD now = GetTickCount();
    run_commands();  // test channel (reads, console text; world commands are queued for the game frame)
    // a load/new map replaces the player: forget the gates here too (memory reads only), otherwise neither update
    // would ever run again -- the power may not be selected and the old gate entities are gone
    uintptr_t p = player();
    if (S->lastPlayer && p != S->lastPlayer && TryEnterCriticalSection(&g_cs)) {  // busy: next tick
        if (S->lastPlayer != p) {
            vg_log("player entity changed %p -> %p (seen from the window thread): gates cleared", (void *)S->lastPlayer,
                   (void *)p);
            memset(S->gate, 0, sizeof S->gate);
            memset(S->passed, 0, sizeof S->passed);
            S->next = 0, S->exitGate = -1, S->stuckGate = -1, S->pendingVOn = 0, S->rescueCheckAt = 0;
            S->lastPlayer = p;
        }
        LeaveCriticalSection(&g_cs);
    }
    if (S->updates != lastCount) {
        lastCount = S->updates;
        lastSeen = now;
        if (warned)
            vg_log("Void Gate updates resumed");
        warned = 0;
    }
    if ((S->gate[0].valid || S->gate[1].valid) && lastSeen && now - lastSeen > 1500 && !warned) {
        warned = 1;
        vg_log("no Void Gate updates for %lu ms (power not selected?)", now - lastSeen);
    }
}

int __declspec(dllexport) vg_module_init(const vg_host *host)
{
    if (host->version != VG_HOST_VERSION)
        return 0;
    g_host = host;
    InitializeCriticalSection(&g_cs);
    InitializeCriticalSection(&g_replyCs);
    InitializeCriticalSection(&g_pendingCs);
    S = (vg_state *)host->state;
    if (S->magic != STATE_MAGIC) {
        // gate_t and its place right after the magic unchanged since 0x5647000D: keep the gates (and their
        // entities) across a reload of an older build
        gate_t keep[2];
        int keepGates = S->magic >= 0x56470012u && S->magic < STATE_MAGIC;  // gate_t changed in 12
        uintptr_t keepPlayer = 0;
        int keepNext = S->next;
        if (keepGates)
            memcpy(keep, S->gate, sizeof keep), keepPlayer = S->lastPlayer;
        memset(S, 0, sizeof *S);
        if (keepGates) {
            memcpy(S->gate, keep, sizeof keep);
            S->lastPlayer = keepPlayer;
            S->next = keepNext;
        }
        S->magic = STATE_MAGIC;
        S->exitGate = -1, S->stuckGate = -1, S->lastFrom = -1;
        S->npcDirect = 1;  // NPCs: Teleport_r + flag, like the level scripts (setviewpos only moves the player)
    }
    vg_log("Void Gate module ready (state %s)", S->gate[0].valid || S->gate[1].valid ? "kept" : "fresh");
    return 1;
}
