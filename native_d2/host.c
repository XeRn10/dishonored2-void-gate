// Stable part of the Void Gate mod, linked into dinput8.dll: installs the hooks once and forwards them to
// voidgate.dll, which it (re)loads whenever that file changes, so the gameplay logic can be rebuilt and swapped
// while the game is running (tools\d2run.ps1 -Module).
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "host.h"

void vg_log(const char *fmt, ...);  // proxy.c


#define VT_GRAPPLE 0x141E86EE8ull  // arkCpntPowerGrapple::vftable
#define SLOT_CAST 36
#define FN_GRAPPLE_CAST 0x140500240ull
#define IAT_PEEKMESSAGEA 0x141C448B0ull
// The component scheduler updates every active arkCpntPowerGrapple through one direct call (in 0x140B06290); the
// gate logic runs right after it, on the same game job thread as Far Reach's own FX and pulls.
#define CALL_GRAPPLE_UPDATE 0x140B062D1ull
#define FN_GRAPPLE_UPDATE 0x140500CE0ull
// Same for arkCpntFX (loop 0x140B00250): the gate entities' FX components update every frame whatever power is
// selected, so the gates keep working after switching powers.
#define CALL_FX_UPDATE 0x140B00291ull
#define FN_FX_UPDATE 0x14058E040ull
// arkCpntPlayerPhysics updates every frame whatever the player holds (arkCpntPlayerPhysicsFactory slot 4, loop
// 0x140AFCA50): the gates' always-on tick (cpntFX turned out not to be updated every frame)
#define CALL_PHYS_UPDATE 0x140AFCA91ull
#define FN_PHYS_UPDATE 0x140967730ull
// arkPlayerStateFalling update (vtable slot 13), which adds up the fall height: the module zeroes it while a gate
// protects the player. arkPlayerStateDropReady (falling with a drop assassination ready) calls it directly.
#define VT_FALLING 0x141EF5C98ull
#define SLOT_FALLING_UPDATE 13
#define FN_FALLING_UPDATE 0x1409E2950ull
#define CALL_DROPREADY_FALLING 0x1409A35C5ull
// arkHUDElementStatusInformation (the "[key] action" prompts under the crosshair), in its update 0x140529260: the
// last state check before it looks at the "rebuild" flag (which it clears on entry), and the call that pushes a
// rebuilt prompt list to the UI. The module adds the "[F] Close the gates" prompt.
#define CALL_HUD_STATUS_CHECK 0x140529314ull
#define FN_HUD_STATUS_CHECK 0x140529CD0ull
#define CALL_HUD_STATUS_PUSH 0x1405293BAull
// the grapple's aim marker update (0x1405047A0) moves its FX to the spot Far Reach would pull to: the module puts
// it on the aimed surface instead, in that same call (moving it again afterwards made it jitter between the two)
#define CALL_AIM_FX_MOVE 0x140504D88ull
#define FN_FX_MOVE 0x1409FC6B0ull
// main view submit (game thread, after the player camera update; docs/d2_portal_render_research.md 5.1): called
// from three places
#define FN_VIEW_SUBMIT 0x140A393A0ull
// idRenderSystemLocal::RenderPortalView (vtable 0x141E704F8 slot 0x1C8): renders the armed second view before the main
// view of a frame -- and reads the frame's main view without checking it: a frame without one (a load started right
// after the module armed the view) crashed there. The guard drops the armed view instead.
#define VT_RENDER_SYSTEM 0x141E704F8ull
#define SLOT_RENDER_PORTAL (0x1C8 / 8)
#define FN_RENDER_PORTAL 0x140409AA0ull
static const uintptr_t CALLS_VIEW_SUBMIT[] = {0x140A35E52ull, 0x140A390C8ull, 0x140A39231ull};
#define FN_HUD_STATUS_PUSH 0x14052ABE0ull

typedef BOOL(WINAPI *PeekMessageA_t)(LPMSG, HWND, UINT, UINT, UINT);
typedef void (*cpnt_fn)(uintptr_t comp);

static PeekMessageA_t g_realPeek;
static char g_dir[MAX_PATH];
static unsigned char g_state[VG_STATE_SIZE] __attribute__((aligned(16)));
static vg_host g_host;

static SRWLOCK g_modLock = SRWLOCK_INIT;
static HMODULE g_mod;
static vg_cast_fn g_modCast;
static vg_tick_fn g_modTick;
static vg_update_fn g_modUpdate, g_modFxUpdate, g_modFrame, g_modFalling;
static vg_hud_fn g_modHud;
static vg_aim_fn g_modAim;
static vg_view_fn g_modView;
static char g_modPath[MAX_PATH];
static FILETIME g_modTime;
static int g_modSerial;

// ---------------------------------------------------------------- module loading
static int module_time(FILETIME *t)
{
    char src[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA a;
    snprintf(src, sizeof src, "%svoidgate.dll", g_dir);
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &a))
        return 0;
    *t = a.ftLastWriteTime;
    return 1;
}

static void module_reload(void)
{
    char src[MAX_PATH], live[MAX_PATH];
    FILETIME t;
    if (!module_time(&t))
        return;
    snprintf(src, sizeof src, "%svoidgate.dll", g_dir);
    // load a copy so voidgate.dll itself stays replaceable
    snprintf(live, sizeof live, "%svoidgate.live%d.dll", g_dir, ++g_modSerial);
    if (!CopyFileA(src, live, FALSE)) {
        vg_log("module: copy to %s failed (%lu)", live, GetLastError());
        return;
    }
    HMODULE m = LoadLibraryA(live);
    vg_init_fn init = m ? (vg_init_fn)GetProcAddress(m, "vg_module_init") : NULL;
    vg_cast_fn cast = m ? (vg_cast_fn)GetProcAddress(m, "vg_module_cast") : NULL;
    vg_tick_fn tick = m ? (vg_tick_fn)GetProcAddress(m, "vg_module_tick") : NULL;
    vg_update_fn update = m ? (vg_update_fn)GetProcAddress(m, "vg_module_update") : NULL;  // optional
    vg_update_fn fxUpdate = m ? (vg_update_fn)GetProcAddress(m, "vg_module_fx_update") : NULL;  // optional
    vg_update_fn frame = m ? (vg_update_fn)GetProcAddress(m, "vg_module_frame") : NULL;  // optional
    vg_update_fn falling = m ? (vg_update_fn)GetProcAddress(m, "vg_module_falling") : NULL;  // optional
    vg_hud_fn hud = m ? (vg_hud_fn)GetProcAddress(m, "vg_module_hud") : NULL;  // optional
    vg_aim_fn aim = m ? (vg_aim_fn)GetProcAddress(m, "vg_module_aim_move") : NULL;  // optional
    vg_view_fn view = m ? (vg_view_fn)GetProcAddress(m, "vg_module_view") : NULL;  // optional
    if (!m || !init || !cast || !tick) {
        vg_log("module: %s is not a Void Gate module (load %p, err %lu)", live, (void *)m, GetLastError());
        if (m)
            FreeLibrary(m);
        DeleteFileA(live);
        g_modTime = t;  // don't retry this build every tick
        return;
    }
    AcquireSRWLockExclusive(&g_modLock);
    HMODULE old = g_mod;
    char oldPath[MAX_PATH];
    lstrcpyA(oldPath, g_modPath);
    g_mod = NULL, g_modCast = NULL, g_modTick = NULL, g_modUpdate = NULL, g_modFxUpdate = NULL, g_modFrame = NULL;
    g_modFalling = NULL, g_modHud = NULL, g_modAim = NULL, g_modView = NULL;
    if (init(&g_host)) {
        g_mod = m, g_modCast = cast, g_modTick = tick, g_modUpdate = update, g_modFxUpdate = fxUpdate, g_modFrame = frame;
        g_modFalling = falling, g_modHud = hud, g_modAim = aim, g_modView = view;
        lstrcpyA(g_modPath, live);
    }
    ReleaseSRWLockExclusive(&g_modLock);
    g_modTime = t;
    if (old) {
        FreeLibrary(old);
        DeleteFileA(oldPath);
    }
    if (g_mod)
        vg_log("module: loaded build #%d", g_modSerial);
    else {
        vg_log("module: init refused build #%d", g_modSerial);
        FreeLibrary(m);
        DeleteFileA(live);
    }
}

static void module_poll(void)
{
    static DWORD lastCheck;
    DWORD now = GetTickCount();
    if (now - lastCheck < 500)
        return;
    lastCheck = now;
    FILETIME t;
    if (module_time(&t) && CompareFileTime(&t, &g_modTime) != 0) {
        // wait until the writer is done: the file time must be at least 300 ms old
        FILETIME nowFt;
        GetSystemTimeAsFileTime(&nowFt);
        ULARGE_INTEGER a = {.LowPart = t.dwLowDateTime, .HighPart = t.dwHighDateTime};
        ULARGE_INTEGER b = {.LowPart = nowFt.dwLowDateTime, .HighPart = nowFt.dwHighDateTime};
        if (b.QuadPart - a.QuadPart > 3000000ull)
            module_reload();
    }
}

// ---------------------------------------------------------------- hooks
static void hk_cast(uintptr_t comp)
{
    AcquireSRWLockShared(&g_modLock);
    if (g_modCast)
        g_modCast(comp);
    else
        ((cpnt_fn)FN_GRAPPLE_CAST)(comp);
    ReleaseSRWLockShared(&g_modLock);
}

static void hk_grappleUpdate(uintptr_t comp)
{
    ((cpnt_fn)FN_GRAPPLE_UPDATE)(comp);
    AcquireSRWLockShared(&g_modLock);
    if (g_modUpdate)
        g_modUpdate(comp);
    ReleaseSRWLockShared(&g_modLock);
}

static void hk_physUpdate(uintptr_t comp)
{
    ((cpnt_fn)FN_PHYS_UPDATE)(comp);
    AcquireSRWLockShared(&g_modLock);
    if (g_modFrame)
        g_modFrame(comp);
    ReleaseSRWLockShared(&g_modLock);
}

static void hk_fxUpdate(uintptr_t comp)
{
    ((cpnt_fn)FN_FX_UPDATE)(comp);
    AcquireSRWLockShared(&g_modLock);
    if (g_modFxUpdate)
        g_modFxUpdate(comp);
    ReleaseSRWLockShared(&g_modLock);
}

// before the falling state's update; the extra registers are passed through untouched
typedef void (*state4_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static void hk_fallingUpdate(uintptr_t state, uintptr_t b, uintptr_t c, uintptr_t d)
{
    AcquireSRWLockShared(&g_modLock);
    if (g_modFalling)
        g_modFalling(state);
    ReleaseSRWLockShared(&g_modLock);
    ((state4_fn)FN_FALLING_UPDATE)(state, b, c, d);
}

static void hud_call(uintptr_t elem, int phase)
{
    AcquireSRWLockShared(&g_modLock);
    if (g_modHud)
        g_modHud(elem, phase);
    ReleaseSRWLockShared(&g_modLock);
}

static void hk_hudStatusCheck(uintptr_t elem, uintptr_t b, uintptr_t c, uintptr_t d)
{
    ((state4_fn)FN_HUD_STATUS_CHECK)(elem, b, c, d);
    hud_call(elem, 0);
}

static void hk_hudStatusPush(uintptr_t elem, uintptr_t b, uintptr_t c, uintptr_t d)
{
    hud_call(elem, 1);
    ((state4_fn)FN_HUD_STATUS_PUSH)(elem, b, c, d);
}

static void hk_aimFxMove(uintptr_t owner, uintptr_t handle, uintptr_t pos, uintptr_t axis)
{
    float p[3], ax[9];
    memcpy(p, (void *)pos, sizeof p);
    memcpy(ax, (void *)axis, sizeof ax);
    AcquireSRWLockShared(&g_modLock);
    if (g_modAim)
        g_modAim((uint32_t)handle, p, ax);
    ReleaseSRWLockShared(&g_modLock);
    ((state4_fn)FN_FX_MOVE)(owner, handle, (uintptr_t)p, (uintptr_t)ax);
}

typedef uintptr_t (*ret4_fn)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
static uintptr_t hk_viewSubmit(uintptr_t game, uintptr_t frame, uintptr_t c, uintptr_t d)
{
    uintptr_t r = ((ret4_fn)FN_VIEW_SUBMIT)(game, frame, c, d);
    AcquireSRWLockShared(&g_modLock);
    if (g_modView)
        g_modView(game, frame);
    ReleaseSRWLockShared(&g_modLock);
    return r;
}

static void hk_renderPortal(uintptr_t rs, uintptr_t frameEntry)
{
    if (*(uintptr_t *)(rs + 0x88) && (!frameEntry || !*(uintptr_t *)(frameEntry + 0x28))) {
        *(uintptr_t *)(rs + 0x88) = 0;  // no main view this frame: no second view either
        return;
    }
    ((void (*)(uintptr_t, uintptr_t))FN_RENDER_PORTAL)(rs, frameEntry);
}

static BOOL WINAPI hk_PeekMessageA(LPMSG m, HWND h, UINT a, UINT b, UINT r)
{
    static DWORD last, firstThread;
    DWORD now = GetTickCount();
    if (!firstThread) {
        firstThread = GetCurrentThreadId();
        vg_log("tick thread %lu", firstThread);
    }
    if (GetCurrentThreadId() == firstThread && now - last >= 8) {
        last = now;
        module_poll();
        AcquireSRWLockShared(&g_modLock);
        if (g_modTick)
            g_modTick();
        ReleaseSRWLockShared(&g_modLock);
    }
    return g_realPeek(m, h, a, b, r);
}

// ---------------------------------------------------------------- install
static int patch_ptr(uintptr_t at, uintptr_t expect, uintptr_t value)
{
    DWORD old;
    if (*(uintptr_t *)at != expect && expect)
        return 0;
    if (!VirtualProtect((void *)at, 8, PAGE_READWRITE, &old))
        return 0;
    *(uintptr_t *)at = value;
    VirtualProtect((void *)at, 8, old, &old);
    return 1;
}

// ---------------------------------------------------------------- crash reporter
// Logs faults with registers and an unwound call stack (module+offset; Dishonored2.exe offsets + 0x140000000 are
// Ghidra addresses). First-chance: the game may still handle some of these itself.
static void sym(uintptr_t a, char *out, size_t n)
{
    HMODULE m = NULL;
    char path[MAX_PATH];
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)a, &m) &&
        GetModuleFileNameA(m, path, MAX_PATH)) {
        const char *base = strrchr(path, '\\');
        snprintf(out, n, "%s+0x%llX", base ? base + 1 : path, (unsigned long long)(a - (uintptr_t)m));
    } else
        snprintf(out, n, "0x%llX", (unsigned long long)a);
}

static LONG CALLBACK crash_handler(EXCEPTION_POINTERS *ep)
{
    static volatile LONG reports;
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION && code != EXCEPTION_BREAKPOINT &&
        code != EXCEPTION_STACK_OVERFLOW && code != 0xC0000409 /* fast fail */)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&reports) > 8)
        return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT c = *ep->ContextRecord;
    char s[160];
    sym(c.Rip, s, sizeof s);
    vg_log("=== EXCEPTION %08lX at %s (thread %lu) ===", code, s, GetCurrentThreadId());
    if (code == EXCEPTION_ACCESS_VIOLATION)
        vg_log("  %s address %p", ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "writing" : "reading",
               (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    vg_log("  rax %016llX rbx %016llX rcx %016llX rdx %016llX", c.Rax, c.Rbx, c.Rcx, c.Rdx);
    vg_log("  rsi %016llX rdi %016llX rbp %016llX rsp %016llX", c.Rsi, c.Rdi, c.Rbp, c.Rsp);
    vg_log("  r8  %016llX r9  %016llX r10 %016llX r11 %016llX", c.R8, c.R9, c.R10, c.R11);
    vg_log("  r12 %016llX r13 %016llX r14 %016llX r15 %016llX", c.R12, c.R13, c.R14, c.R15);
    if (code == EXCEPTION_STACK_OVERFLOW)
        return EXCEPTION_CONTINUE_SEARCH;
    for (int i = 0; i < 40 && c.Rip; i++) {
        sym(c.Rip, s, sizeof s);
        vg_log("  #%-2d %s", i, s);
        DWORD64 imageBase;
        PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(c.Rip, &imageBase, NULL);
        if (!fn) {  // leaf function: return address is at [rsp]
            c.Rip = *(DWORD64 *)c.Rsp;
            c.Rsp += 8;
            continue;
        }
        void *handlerData;
        DWORD64 establisher;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, fn, &c, &handlerData, &establisher, NULL);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// executable memory within +-2 GB of `near` (for a rel32 call to reach it)
static unsigned char *alloc_near(uintptr_t near_, size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    for (uintptr_t d = gran; d < 0x70000000ull; d += gran) {
        uintptr_t lo = (near_ & ~(gran - 1)) - d, hi = (near_ & ~(gran - 1)) + d;
        void *p = VirtualAlloc((void *)lo, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!p)
            p = VirtualAlloc((void *)hi, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p)
            return p;
    }
    return NULL;
}

// Redirect a `call rel32` at `at` (which must call `expect`) to `hook` through a near `jmp [rip]` stub.
static int patch_call(uintptr_t at, uintptr_t expect, void *hook)
{
    unsigned char *ip = (unsigned char *)at;
    if (ip[0] != 0xE8 || at + 5 + *(int32_t *)(ip + 1) != expect)
        return 0;
    unsigned char *stub = alloc_near(at, 64);
    if (!stub)
        return 0;
    stub[0] = 0xFF, stub[1] = 0x25, *(uint32_t *)(stub + 2) = 0;
    *(uint64_t *)(stub + 6) = (uint64_t)hook;
    FlushInstructionCache(GetCurrentProcess(), stub, 14);
    DWORD old;
    VirtualProtect(ip, 5, PAGE_EXECUTE_READWRITE, &old);
    *(int32_t *)(ip + 1) = (int32_t)((uintptr_t)stub - (at + 5));
    VirtualProtect(ip, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), ip, 5);
    return 1;
}

static DWORD WINAPI install_iat(LPVOID arg)
{
    (void)arg;
    // the loader may still be binding imports while DllMain runs; wait for the real PeekMessageA in the slot
    FARPROC real = GetProcAddress(GetModuleHandleA("user32.dll"), "PeekMessageA");
    for (int i = 0; i < 400; i++) {
        uintptr_t cur = *(uintptr_t *)IAT_PEEKMESSAGEA;
        if (cur && cur != (uintptr_t)hk_PeekMessageA) {
            g_realPeek = (PeekMessageA_t)cur;
            patch_ptr(IAT_PEEKMESSAGEA, cur, (uintptr_t)hk_PeekMessageA);
            vg_log("PeekMessageA IAT hooked (was %p, user32 %p)", (void *)cur, (void *)real);
            return 0;
        }
        Sleep(25);
    }
    vg_log("PeekMessageA IAT never got bound - per-frame logic disabled");
    return 0;
}

static void remove_stale_copies(void)
{
    char pat[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAA fd;
    snprintf(pat, sizeof pat, "%svoidgate.live*.dll", g_dir);
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE)
        return;
    do {
        snprintf(path, sizeof path, "%s%s", g_dir, fd.cFileName);
        DeleteFileA(path);
    } while (FindNextFileA(f, &fd));
    FindClose(f);
}

// Is this the game build the mod was made for? Not by its timestamp (another store's exe of the same build has its
// own): every game address the mod uses must hold the bytes it expects (vg_sig.h, tools/make_sig.py). Any
// difference: the mod stays off -- no hook, no call into the game -- and says so.
#include "vg_sig.h"
static DWORD WINAPI unsupported_box(LPVOID text)
{
    MessageBoxA(NULL, (const char *)text, "Void Gate", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND | MB_TOPMOST);
    return 0;
}
static int build_supported(HMODULE exe, DWORD ts)
{
    if ((uintptr_t)exe != 0x140000000ull)
        return 0;
    int bad = 0, n = (int)(sizeof VG_SIG / sizeof VG_SIG[0]);
    for (int i = 0; i < n; i++) {
        MEMORY_BASIC_INFORMATION m;
        const void *p = (const void *)VG_SIG[i].va;
        if (!VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || memcmp(p, VG_SIG[i].b, sizeof VG_SIG[i].b)) {
            if (bad++ < 5)
                vg_log("  code check: %p differs", p);
        }
    }
    if (GetEnvironmentVariableA("VG_FORCE_UNSUPPORTED", NULL, 0))
        bad = n, vg_log("  code check: forced off (VG_FORCE_UNSUPPORTED, a test of the unsupported path)");
    if (bad)
        vg_log("this Dishonored2.exe build (timestamp %08lX) is not the one Void Gate was made for: %d of %d code "
               "checks differ", ts, bad, n);
    else if (ts != VG_SIG_TIMESTAMP)
        vg_log("Dishonored2.exe timestamp %08lX (made for %08X), but all %d code checks match: enabled", ts,
               VG_SIG_TIMESTAMP, n);
    return !bad;
}

void vg_start(const char *dll_dir)
{
    HMODULE exe = GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)exe + ((IMAGE_DOS_HEADER *)exe)->e_lfanew);
    if (!build_supported(exe, nt->FileHeader.TimeDateStamp)) {
        vg_log("mod disabled: the game runs unmodded");
        static const char text[] =
            "Void Gate mod: this build of Dishonored 2 is not supported (the mod was made and tested on the GOG "
            "version 1.77.9.0).\n\nThe mod has turned itself off - the game runs normally. To remove it, run the "
            "Void Gate installer and choose Uninstall.";
        CreateThread(NULL, 0, unsupported_box, (LPVOID)text, 0, NULL);  // (not under the loader lock)
        return;
    }
    lstrcpynA(g_dir, dll_dir, MAX_PATH);
    AddVectoredExceptionHandler(1, crash_handler);
    g_host.version = VG_HOST_VERSION;
    g_host.log = vg_log;
    g_host.state = g_state;
    g_host.dir = g_dir;
    g_host.origCast = *(uintptr_t *)(VT_GRAPPLE + SLOT_CAST * 8);
    remove_stale_copies();  // the module itself is loaded by the first tick (not under the loader lock)
    if (patch_ptr(VT_GRAPPLE + SLOT_CAST * 8, FN_GRAPPLE_CAST, (uintptr_t)hk_cast))
        vg_log("cast hook installed (arkCpntPowerGrapple slot %d)", SLOT_CAST);
    else
        vg_log("cast hook NOT installed: unexpected vtable value %p", *(void **)(VT_GRAPPLE + SLOT_CAST * 8));
    if (patch_call(CALL_GRAPPLE_UPDATE, FN_GRAPPLE_UPDATE, (void *)hk_grappleUpdate))
        vg_log("update hook installed (grapple update call at %p)", (void *)CALL_GRAPPLE_UPDATE);
    else
        vg_log("update hook NOT installed: unexpected bytes at %p", (void *)CALL_GRAPPLE_UPDATE);
    if (patch_call(CALL_FX_UPDATE, FN_FX_UPDATE, (void *)hk_fxUpdate))
        vg_log("FX update hook installed (cpntFX update call at %p)", (void *)CALL_FX_UPDATE);
    else
        vg_log("FX update hook NOT installed: unexpected bytes at %p", (void *)CALL_FX_UPDATE);
    if (patch_call(CALL_PHYS_UPDATE, FN_PHYS_UPDATE, (void *)hk_physUpdate))
        vg_log("frame hook installed (player physics update call at %p)", (void *)CALL_PHYS_UPDATE);
    else
        vg_log("frame hook NOT installed: unexpected bytes at %p", (void *)CALL_PHYS_UPDATE);
    if (patch_ptr(VT_FALLING + SLOT_FALLING_UPDATE * 8, FN_FALLING_UPDATE, (uintptr_t)hk_fallingUpdate))
        vg_log("falling hook installed (arkPlayerStateFalling slot %d)", SLOT_FALLING_UPDATE);
    else
        vg_log("falling hook NOT installed: unexpected vtable value %p",
               *(void **)(VT_FALLING + SLOT_FALLING_UPDATE * 8));
    if (patch_call(CALL_DROPREADY_FALLING, FN_FALLING_UPDATE, (void *)hk_fallingUpdate))
        vg_log("falling hook installed (drop-ready call at %p)", (void *)CALL_DROPREADY_FALLING);
    else
        vg_log("falling hook NOT installed: unexpected bytes at %p", (void *)CALL_DROPREADY_FALLING);
    if (patch_call(CALL_HUD_STATUS_CHECK, FN_HUD_STATUS_CHECK, (void *)hk_hudStatusCheck) &&
        patch_call(CALL_HUD_STATUS_PUSH, FN_HUD_STATUS_PUSH, (void *)hk_hudStatusPush))
        vg_log("HUD prompt hooks installed");
    else
        vg_log("HUD prompt hooks NOT installed (or only half)");
    if (patch_call(CALL_AIM_FX_MOVE, FN_FX_MOVE, (void *)hk_aimFxMove))
        vg_log("aim marker hook installed (call at %p)", (void *)CALL_AIM_FX_MOVE);
    else
        vg_log("aim marker hook NOT installed: unexpected bytes at %p", (void *)CALL_AIM_FX_MOVE);
    if (patch_ptr(VT_RENDER_SYSTEM + SLOT_RENDER_PORTAL * 8, FN_RENDER_PORTAL, (uintptr_t)hk_renderPortal))
        vg_log("second view guard installed (render system slot 0x1C8)");
    else
        vg_log("second view guard NOT installed: unexpected vtable value");
    for (int k = 0; k < 3; k++)
        if (patch_call(CALLS_VIEW_SUBMIT[k], FN_VIEW_SUBMIT, (void *)hk_viewSubmit))
            vg_log("view submit hook installed (call at %p)", (void *)CALLS_VIEW_SUBMIT[k]);
        else
            vg_log("view submit hook NOT installed: unexpected bytes at %p", (void *)CALLS_VIEW_SUBMIT[k]);
    CreateThread(NULL, 0, install_iat, NULL, 0, NULL);
}
