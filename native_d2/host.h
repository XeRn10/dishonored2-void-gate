// Interface between the stable dinput8.dll host (hooks, reload) and the hot-reloadable voidgate.dll module.
#pragma once
#include <stdint.h>

#define VG_HOST_VERSION 1
#define VG_STATE_SIZE 0x10000

typedef struct vg_host {
    int version;
    void (*log)(const char *fmt, ...);
    void *state;          // VG_STATE_SIZE bytes owned by the host: survives module reloads
    const char *dir;      // game folder, with trailing backslash
    uintptr_t origCast;   // arkCpntPowerGrapple cast (vtable slot 36) as found before hooking
} vg_host;

// voidgate.dll exports
typedef int (*vg_init_fn)(const vg_host *host);   // returns 1 if the module accepted the host
typedef void (*vg_cast_fn)(uintptr_t comp);      // replaces arkCpntPowerGrapple cast (for every grapple)
typedef void (*vg_tick_fn)(void);                // every ~8 ms on the window message thread: no game calls here
typedef void (*vg_update_fn)(uintptr_t comp);    // after arkCpntPowerGrapple's per-frame update (game job thread)
typedef void (*vg_hud_fn)(uintptr_t elem, int phase);
// the aim marker is about to be moved (FX handle, position, 3x3 axis): the module may rewrite pos/axis in place
typedef void (*vg_aim_fn)(uint32_t handle, float *pos, float *axis);
// the main view has just been submitted for this frame (game thread): the module arms the see-through view
typedef void (*vg_view_fn)(uintptr_t game, uintptr_t frame);
