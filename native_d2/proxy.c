// dinput8.dll proxy (x64) for Dishonored 2: forwards DirectInput to the system DLL and hosts the Void Gate mod.
// Built with: zig cc -target x86_64-windows-gnu (see build.cmd)
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

typedef HRESULT(WINAPI *DirectInput8Create_t)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
typedef HRESULT(WINAPI *DllCanUnloadNow_t)(void);
typedef HRESULT(WINAPI *DllGetClassObject_t)(REFCLSID, REFIID, LPVOID *);

static HMODULE g_real;
static char g_logPath[MAX_PATH];
static CRITICAL_SECTION g_logLock;

void vg_log(const char *fmt, ...)
{
    EnterCriticalSection(&g_logLock);
    FILE *f = fopen(g_logPath, "a");
    if (f) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    LeaveCriticalSection(&g_logLock);
}

static FARPROC real_proc(const char *name)
{
    if (!g_real) {
        char path[MAX_PATH];
        GetSystemDirectoryA(path, MAX_PATH);
        lstrcatA(path, "\\dinput8.dll");
        g_real = LoadLibraryA(path);
        vg_log("loaded system dinput8 from %s -> %p", path, (void *)g_real);
    }
    return g_real ? GetProcAddress(g_real, name) : NULL;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE h, DWORD ver, REFIID riid, LPVOID *out, LPUNKNOWN outer)
{
    DirectInput8Create_t fn = (DirectInput8Create_t)real_proc("DirectInput8Create");
    return fn ? fn(h, ver, riid, out, outer) : E_FAIL;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    DllCanUnloadNow_t fn = (DllCanUnloadNow_t)real_proc("DllCanUnloadNow");
    return fn ? fn() : S_FALSE;
}

HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID *out)
{
    DllGetClassObject_t fn = (DllGetClassObject_t)real_proc("DllGetClassObject");
    return fn ? fn(clsid, riid, out) : E_FAIL;
}

void vg_start(const char *dll_dir);  // vg.c

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        InitializeCriticalSection(&g_logLock);
        GetModuleFileNameA(inst, g_logPath, MAX_PATH);
        char *slash = strrchr(g_logPath, '\\');
        char dir[MAX_PATH];
        lstrcpyA(dir, g_logPath);
        dir[slash ? slash - g_logPath + 1 : 0] = 0;
        lstrcpyA(slash ? slash + 1 : g_logPath, "dmod_d2.log");
        DeleteFileA(g_logPath);
        vg_log("Void Gate mod attached, exe base %p", (void *)GetModuleHandleA(NULL));
        vg_start(dir);
    }
    return TRUE;
}
