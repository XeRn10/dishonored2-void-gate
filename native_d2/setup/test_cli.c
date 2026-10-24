// Test driver for the native setup's core (no window): test_cli.exe <game folder> install|uninstall|check|changed
#include <stdio.h>
#include <wchar.h>
#include <windows.h>

#include "core.h"

static void say_cb(const char *en, const char *ru)
{
    (void)ru;
    printf("  %s\n", en);
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 3)
        return 2;
    vg_say = say_cb;
    const wchar_t *game = argv[1], *what = argv[2];
    if (!wcscmp(what, L"check")) {
        int bad, n;
        int ok = vg_check_build(game, &bad, &n);
        printf("is_game %d, build ok %d (%d of %d differ), installed %d, writable %d\n", vg_is_game(game), ok, bad, n,
               vg_installed(game), vg_writable(game));
        wchar_t found[MAX_PATH];
        if (vg_find_game(NULL, found))
            printf("found game: %ls\n", found);
        return 0;
    }
    if (!wcscmp(what, L"changed")) {
        printf("changed since install: %d\n", vg_changed_since_install(game));
        return 0;
    }
    int ok = !wcscmp(what, L"install") ? vg_install(game) : vg_uninstall(game);
    if (!ok)
        printf("FAILED: %s\n", vg_last_error());
    return ok ? 0 : 1;
}
