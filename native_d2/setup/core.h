// The native setup's work (core.c, a port of tools/vg_setup.py): find the game, check its build, install Void Gate on
// top of the current mod catalogs, uninstall back to the state before it.
#pragma once
#include <wchar.h>

// progress / result messages, English and Russian (UTF-8); set by the window or the command line
extern void (*vg_say)(const char *en, const char *ru);

int vg_is_game(const wchar_t *dir);
int vg_find_game(const wchar_t *hint, wchar_t *out);       // 1 when found (out: MAX_PATH)
int vg_check_build(const wchar_t *game, int *bad, int *n);  // 1 when every code check matches
int vg_game_running(void);
int vg_writable(const wchar_t *game);
int vg_installed(const wchar_t *game);             // a Void Gate backup (and its state) is in the game folder
int vg_changed_since_install(const wchar_t *game); // another tool rewrote the mod catalogs after our install
int vg_install(const wchar_t *game);               // 1 on success; messages through vg_say
int vg_uninstall(const wchar_t *game);
const char *vg_last_error(void);
