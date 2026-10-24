// Native setup: the window. Pages Welcome -> Game folder -> Installing -> Done, or (VoidGate_Uninstall.exe, or
// "Uninstall" on the first page) Uninstall -> Removing -> Done. Dark, the rifts' violet as the accent; everything is
// painted here except the folder's edit box. English by default, Russian by the switch at the bottom left.
// Sizes are at 96 dpi and scaled to the screen.
//   VoidGateSetup.exe [game folder] [--lang=en|ru] [--go=install|uninstall]   (--go: after a restart for admin rights)
#define COBJMACROS
#include <windows.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <wincodec.h>

#include "core.h"
#include "payload.h"

#define RGBX(c) RGB((c) >> 16 & 255, (c) >> 8 & 255, (c) & 255)
enum {
    C_BG = 0x16151c, C_BG2 = 0x1f1d27, C_FIELD = 0x24222e, C_BORDER = 0x3a3747, C_FG = 0xece9f4, C_MUTED = 0xa9a4b8,
    C_ACCENT = 0x8a63f0, C_ACCENT_HI = 0x9d7bff, C_BTN = 0x2b2936, C_BTN_HI = 0x3a3747, C_OK = 0x7fd18b,
    C_WARN = 0xf0b44c, C_ERR = 0xff7a7a
};

// ---------------------------------------------------------------- texts
enum {
    S_TITLE, S_TITLE_UN, S_NAME, S_TAGLINE, S_WELCOME, S_INSTALLED, S_REINSTALL, S_UNINSTALL, S_FOLDER_H, S_FOLDER,
    S_BROWSE, S_NOT_GAME, S_BUILD_OK, S_BUILD_OTHER, S_RUNNING, S_UN_H, S_UN_TEXT, S_NOT_INSTALLED, S_CHANGED,
    S_WORKING_IN, S_WORKING_UN, S_WAIT, S_DONE_H, S_DONE, S_REMOVED_H, S_REMOVED, S_SELF, S_FAIL_H, S_ADMIN, S_NOWRITE,
    S_BACK, S_NEXT, S_CANCEL, S_INSTALL, S_FINISH, S_CLOSE, S_COUNT
};
static const char *const T[S_COUNT][2] = {
    {"Void Gate %s Setup", "Установка «Врат Бездны» %s"},
    {"Void Gate %s: Uninstall", "Удаление «Врат Бездны» %s"},
    {"Void Gate", "Врата Бездны"},
    {"A Portal-style power for Dishonored 2", "Сила в стиле Portal для Dishonored 2"},
    {"Open two linked rifts into the Void on any wall, floor or ceiling, step into one and come out of the other, "
     "keeping your speed.\n\nThe setup adds Void Gate to the game on top of the mods you already have. Close "
     "Dishonored 2 before you continue.",
     "Откройте два связанных разлома в Бездну на любой стене, полу или потолке, войдите в один и выйдите из другого, "
     "сохранив скорость.\n\nУстановщик добавит Врата в игру поверх уже установленных модов. Перед продолжением "
     "закройте Dishonored 2."},
    {"Void Gate is already installed in this game.", "Врата Бездны уже установлены в этой игре."},
    {"Reinstall (update)", "Переустановить (обновить)"},
    {"Uninstall", "Удалить"},
    {"Game folder", "Папка игры"},
    {"The Dishonored 2 folder (the one with Dishonored2.exe):", "Папка Dishonored 2 (та, где лежит Dishonored2.exe):"},
    {"Browse...", "Обзор..."},
    {"This folder has no Dishonored 2 (Dishonored2.exe).", "В этой папке нет Dishonored 2 (Dishonored2.exe)."},
    {"Game version: supported. Its code matches the version the mod was made and tested on (GOG 1.77.9.0).",
     "Версия игры: поддерживается. Её код совпадает с версией, на которой мод сделан и проверен (GOG 1.77.9.0)."},
    {"This version of the game has not been tested: the mod was made for GOG 1.77.9.0. You can still install it. At "
     "every start the mod checks the game and turns itself off if it does not fit; the game then runs normally, just "
     "without Void Gate.",
     "Эта версия игры не проверялась: мод сделан для GOG 1.77.9.0. Установить всё равно можно. При каждом запуске "
     "мод проверяет игру и, если она не подходит, выключается сам; игра тогда работает как обычно, просто без Врат "
     "Бездны."},
    {"Dishonored 2 is running. Close the game, then press the button again.",
     "Dishonored 2 запущена. Закройте игру и нажмите кнопку ещё раз."},
    {"Uninstall Void Gate", "Удаление «Врат Бездны»"},
    {"Remove Void Gate from this game and put the game folder back exactly as it was before it?",
     "Удалить Врата Бездны из этой игры и вернуть папку игры точно такой, какой она была до них?"},
    {"Void Gate is not installed in this game.", "Врата Бездны в этой игре не установлены."},
    {"The mod files were changed by another tool after Void Gate was installed (other mods?).\n\nUninstalling puts "
     "back the state from before Void Gate, so mods installed after it are removed too; you can install them again "
     "afterwards.\n\nContinue?",
     "После установки Врат Бездны файлы модов менялись другой программой (другие моды?).\n\nУдаление вернёт "
     "состояние до Врат, поэтому моды, поставленные после них, тоже пропадут; их можно будет поставить заново.\n\n"
     "Продолжить?"},
    {"Installing", "Установка"},
    {"Removing", "Удаление"},
    {"Please wait...", "Подождите..."},
    {"Void Gate is installed", "Врата Бездны установлены"},
    {"Start the game, open the journal, go to Powers and buy Void Gate (4 runes). Then select it on the power wheel."
     "\n\nHold the power button to aim and release it to open a rift. Tap F to close both rifts.\n\nTo remove the "
     "mod, run VoidGate_Uninstall.exe in the game folder.",
     "Запустите игру, откройте журнал, раздел «Умения», и купите Врата Бездны (4 руны). Затем выберите их на колесе "
     "сил.\n\nЗажмите кнопку силы, чтобы прицелиться, и отпустите, чтобы открыть разлом. Короткое нажатие F "
     "закрывает оба разлома.\n\nЧтобы удалить мод, запустите VoidGate_Uninstall.exe в папке игры."},
    {"Void Gate is removed", "Врата Бездны удалены"},
    {"The game folder is as it was before Void Gate. Saves made with Void Gate still load; the power is simply gone.",
     "Папка игры такая же, как до установки Врат Бездны. Сохранения с Вратами загружаются, сила из них просто "
     "пропадает."},
    {"The uninstaller itself, VoidGate_Uninstall.exe, can be deleted now.",
     "Сам деинсталлятор, VoidGate_Uninstall.exe, теперь можно удалить."},
    {"Something went wrong", "Что-то пошло не так"},
    {"The game folder can only be changed with administrator rights. Windows will ask for them now.",
     "Папку игры можно изменить только с правами администратора. Сейчас Windows их запросит."},
    {"Cannot write to the game folder. Close the game and anything that uses its files, then try again.",
     "Не удаётся записать в папку игры. Закройте игру и всё, что использует её файлы, и попробуйте снова."},
    {"< Back", "< Назад"},
    {"Next >", "Далее >"},
    {"Cancel", "Отмена"},
    {"Install", "Установить"},
    {"Finish", "Готово"},
    {"Close", "Закрыть"},
};

static int g_lang;  // 0 English, 1 Russian
static wchar_t *W(const char *u8)  // a UTF-8 text as a wide string (a few small rings of buffers)
{
    static wchar_t ring[8][2048];
    static int k;
    wchar_t *o = ring[k++ & 7];
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, o, 2048);
    return o;
}
static wchar_t *TX(int id) { return W(T[id][g_lang]); }

// ---------------------------------------------------------------- state
enum { P_WELCOME, P_FOLDER, P_UNINSTALL, P_WORKING, P_DONE };
static HWND g_wnd, g_edit;
static HINSTANCE g_inst;
static double g_s = 1.0;  // screen scale
static int g_pct = 100;
static HFONT f_body, f_title, f_head, f_btn, f_small;
static HBITMAP g_side, g_icon;
static int g_sideW, g_sideH, g_iconW, g_iconH;
static HBRUSH b_field;
static int g_page, g_uninstaller, g_choiceUninstall, g_action;  // g_action: 0 install, 1 uninstall
static wchar_t g_game[MAX_PATH];
static int g_folderState;  // 0 not a game, 1 supported, 2 other build, 3 game running, 4 cannot write
static wchar_t g_folderChecked[MAX_PATH];
static int g_resultOk, g_progress;
static wchar_t g_status[1024], g_detail[1024];
static RECT g_editRect;
static int g_hover = -1, g_pressed = -1;

static int S(double v) { return (int)(v * g_s + 0.5); }

// clickable things of the current page (rebuilt at every paint)
enum { A_NONE, A_BACK, A_NEXT, A_CANCEL, A_BROWSE, A_RADIO0, A_RADIO1, A_LANG_EN, A_LANG_RU };
typedef struct {
    RECT r;
    int action, enabled, primary;
    const wchar_t *label;
} hot_t;
static hot_t g_hot[16];
static int g_nhot;

// ---------------------------------------------------------------- pictures (PNG from the payload, via WIC)
static HBITMAP load_png(vg_blob_t b, int *w, int *h)
{
    IWICImagingFactory *f = NULL;
    IWICStream *st = NULL;
    IWICBitmapDecoder *dec = NULL;
    IWICBitmapFrameDecode *fr = NULL;
    IWICFormatConverter *cv = NULL;
    HBITMAP hb = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                                (void **)&f)))
        return NULL;
    if (SUCCEEDED(IWICImagingFactory_CreateStream(f, &st)) &&
        SUCCEEDED(IWICStream_InitializeFromMemory(st, (BYTE *)vg_payload + b.off, b.size)) &&
        SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(f, (IStream *)st, NULL, WICDecodeMetadataCacheOnDemand,
                                                             &dec)) &&
        SUCCEEDED(IWICBitmapDecoder_GetFrame(dec, 0, &fr)) &&
        SUCCEEDED(IWICImagingFactory_CreateFormatConverter(f, &cv)) &&
        SUCCEEDED(IWICFormatConverter_Initialize(cv, (IWICBitmapSource *)fr, &GUID_WICPixelFormat32bppPBGRA,
                                                 WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) {
        UINT ww, hh;
        IWICFormatConverter_GetSize(cv, &ww, &hh);
        BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), (LONG)ww, -(LONG)hh, 1, 32, BI_RGB}};
        void *bits;
        hb = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (hb)
            IWICFormatConverter_CopyPixels(cv, NULL, ww * 4, ww * hh * 4, bits);
        *w = (int)ww, *h = (int)hh;
    }
    if (cv)
        IWICFormatConverter_Release(cv);
    if (fr)
        IWICBitmapFrameDecode_Release(fr);
    if (dec)
        IWICBitmapDecoder_Release(dec);
    if (st)
        IWICStream_Release(st);
    IWICImagingFactory_Release(f);
    return hb;
}

// ---------------------------------------------------------------- drawing helpers
static void fill(HDC dc, RECT r, int c)
{
    HBRUSH b = CreateSolidBrush(RGBX(c));
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static void frame(HDC dc, RECT r, int c)
{
    HBRUSH b = CreateSolidBrush(RGBX(c));
    FrameRect(dc, &r, b);
    DeleteObject(b);
}

static int text(HDC dc, const wchar_t *s, HFONT f, int c, int x, int y, int w, UINT extra)  // returns the height
{
    RECT r = {x, y, x + w, y + 10000};
    SelectObject(dc, f);
    SetTextColor(dc, RGBX(c));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT | extra);
    r.right = x + w;
    DrawTextW(dc, s, -1, &r, DT_WORDBREAK | DT_NOPREFIX | extra);
    return r.bottom - r.top;
}

static void add_hot(RECT r, int action, int enabled, int primary, const wchar_t *label)
{
    if (g_nhot < 16)
        g_hot[g_nhot++] = (hot_t){r, action, enabled, primary, label};
}

static void button(HDC dc, int *right, int y, int action, const wchar_t *label, int enabled, int primary)
{
    SelectObject(dc, f_btn);
    SIZE sz;
    GetTextExtentPoint32W(dc, label, (int)wcslen(label), &sz);
    int w = max(sz.cx + S(32), S(96)), h = S(30);
    RECT r = {*right - w, y, *right, y + h};
    *right -= w + S(8);
    int idx = g_nhot;
    add_hot(r, action, enabled, primary, label);
    int hov = g_hover == idx && enabled;
    int bg = !enabled ? C_BG2 : primary ? (hov ? C_ACCENT_HI : C_ACCENT) : (hov ? C_BTN_HI : C_BTN);
    fill(dc, r, bg);
    if (!primary || !enabled)
        frame(dc, r, C_BORDER);
    SetTextColor(dc, RGBX(!enabled ? C_MUTED : primary ? 0xffffff : C_FG));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static int header(HDC dc, int x, int y, int w, const wchar_t *title, const wchar_t *sub)
{
    if (g_icon) {
        HDC m = CreateCompatibleDC(dc);
        SelectObject(m, g_icon);
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        int sz = S(40);
        GdiAlphaBlend(dc, x, y + (sub ? S(2) : 0), sz, sz, m, 0, 0, g_iconW, g_iconH, bf);
        DeleteDC(m);
    }
    int tx = x + S(52);
    int h = text(dc, title, sub ? f_title : f_head, C_FG, tx, y + (sub ? 0 : S(6)), w - S(52), 0);
    if (sub)
        h += text(dc, sub, f_small, C_MUTED, tx, y + h, w - S(52), 0);
    return max(h, S(44)) + S(16);
}

// ---------------------------------------------------------------- pages
static void check_folder(void)  // the folder page's status line
{
    wchar_t p[MAX_PATH];
    GetWindowTextW(g_edit, p, MAX_PATH);
    if (!wcscmp(p, g_folderChecked) && g_folderState != 3 && g_folderState != 4)
        return;
    wcscpy(g_folderChecked, p);
    if (!vg_is_game(p)) {
        g_folderState = 0;
        return;
    }
    int bad, n;
    g_folderState = vg_check_build(p, &bad, &n) ? 1 : 2;
}

static void place_edit(RECT field)  // the edit box inside the painted field, its text centred vertically
{
    HDC dc = GetDC(g_edit);
    SelectObject(dc, f_body);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    ReleaseDC(g_edit, dc);
    RECT r = {field.left + S(7), field.top + (field.bottom - field.top - tm.tmHeight) / 2, field.right - S(7), 0};
    r.bottom = r.top + tm.tmHeight;
    if (!EqualRect(&r, &g_editRect) || !IsWindowVisible(g_edit)) {
        g_editRect = r;
        SetWindowPos(g_edit, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_SHOWWINDOW);
    }
}

static void paint(HDC dc, RECT cr)
{
    g_nhot = 0;
    fill(dc, cr, C_BG);
    // side picture
    if (g_side) {
        HDC m = CreateCompatibleDC(dc);
        SelectObject(m, g_side);
        SetStretchBltMode(dc, HALFTONE);
        StretchBlt(dc, 0, 0, S(200), cr.bottom, m, 0, 0, g_sideW, g_sideH, SRCCOPY);
        DeleteDC(m);
    }
    int x0 = S(200) + S(28), x1 = cr.right - S(26), w = x1 - x0, y = S(22);
    int barY = cr.bottom - S(50);
    // bottom bar
    RECT bar = {S(200), barY, cr.right, cr.bottom};
    fill(dc, bar, C_BG2);
    RECT line = {S(200), barY, cr.right, barY + 1};
    fill(dc, line, C_BORDER);
    // language switch
    {
        static const wchar_t *const names[2] = {L"English", L"\x0420\x0443\x0441\x0441\x043a\x0438\x0439"};
        int lx = S(200) + S(14), ly = barY + S(13);
        for (int i = 0; i < 2; i++) {
            SelectObject(dc, f_small);
            SIZE sz;
            GetTextExtentPoint32W(dc, names[i], (int)wcslen(names[i]), &sz);
            RECT r = {lx, ly, lx + sz.cx + S(16), ly + S(24)};
            int idx = g_nhot;
            add_hot(r, i ? A_LANG_RU : A_LANG_EN, g_page != P_WORKING, 0, names[i]);
            if (g_lang == i)
                fill(dc, r, C_BTN);
            else if (g_hover == idx)
                fill(dc, r, C_BG);
            SetTextColor(dc, RGBX(g_lang == i ? C_FG : C_MUTED));
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, names[i], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            lx = r.right + S(2);
        }
    }
    int right = cr.right - S(14), by = barY + S(10);
    int installed = vg_installed(g_game);
    switch (g_page) {
    case P_WELCOME:
        y += header(dc, x0, y, w, TX(S_NAME), TX(S_TAGLINE));
        y += text(dc, TX(S_WELCOME), f_body, C_FG, x0, y, w, 0);
        if (installed) {
            y += S(16);
            y += text(dc, TX(S_INSTALLED), f_body, C_OK, x0, y, w, 0) + S(8);
            for (int i = 0; i < 2; i++) {
                RECT r = {x0, y, x0 + w, y + S(26)};
                add_hot(r, i ? A_RADIO1 : A_RADIO0, 1, 0, NULL);
                int sel = g_choiceUninstall == i, c = S(13), cy = y + S(6);
                HBRUSH br = CreateSolidBrush(RGBX(sel ? C_ACCENT : C_FIELD));
                HPEN pen = CreatePen(PS_SOLID, 1, RGBX(sel ? C_ACCENT_HI : C_MUTED));
                SelectObject(dc, br);
                SelectObject(dc, pen);
                Ellipse(dc, x0, cy, x0 + c, cy + c);
                DeleteObject(br);
                DeleteObject(pen);
                text(dc, TX(i ? S_UNINSTALL : S_REINSTALL), f_body, C_FG, x0 + S(22), y + S(3), w - S(22), 0);
                y += S(26);
            }
        }
        button(dc, &right, by, A_CANCEL, TX(S_CANCEL), 1, 0);
        button(dc, &right, by, A_NEXT, TX(S_NEXT), 1, 1);
        break;
    case P_FOLDER: {
        y += header(dc, x0, y, w, TX(S_FOLDER_H), NULL);
        y += text(dc, TX(S_FOLDER), f_body, C_FG, x0, y, w, 0) + S(6);
        RECT er = {x0, y, x0 + w - S(104), y + S(28)};
        fill(dc, er, C_FIELD);
        frame(dc, er, GetFocus() == g_edit ? C_ACCENT : C_BORDER);
        place_edit(er);
        int bright = x1;
        button(dc, &bright, y - S(1), A_BROWSE, TX(S_BROWSE), 1, 0);
        y += S(28) + S(14);
        int c = g_folderState == 1 ? C_OK : g_folderState == 2 ? C_WARN : C_ERR;
        int sid = g_folderState == 0 ? S_NOT_GAME : g_folderState == 1 ? S_BUILD_OK : g_folderState == 2 ? S_BUILD_OTHER
                  : g_folderState == 3                               ? S_RUNNING
                                                                     : S_NOWRITE;
        wchar_t st[1200];
        _snwprintf(st, 1200, L"%ls%ls", g_folderState == 1 ? L"\x2713  " : g_folderState == 2 ? L"\x26A0  " : L"",
                   TX(sid));
        text(dc, st, f_body, c, x0, y, w, 0);
        button(dc, &right, by, A_CANCEL, TX(S_CANCEL), 1, 0);
        button(dc, &right, by, A_NEXT, TX(S_INSTALL), g_folderState != 0, 1);
        button(dc, &right, by, A_BACK, TX(S_BACK), 1, 0);
        break;
    }
    case P_UNINSTALL:
        y += header(dc, x0, y, w, TX(S_UN_H), NULL);
        if (!installed) {
            text(dc, TX(S_NOT_INSTALLED), f_body, C_ERR, x0, y, w, 0);
            button(dc, &right, by, A_CANCEL, TX(S_CLOSE), 1, 1);
            break;
        }
        y += text(dc, TX(S_UN_TEXT), f_body, C_FG, x0, y, w, 0) + S(12);
        text(dc, g_game, f_small, C_MUTED, x0, y, w, DT_EDITCONTROL);
        button(dc, &right, by, A_CANCEL, TX(S_CANCEL), 1, 0);
        button(dc, &right, by, A_NEXT, TX(S_UNINSTALL), 1, 1);
        if (!g_uninstaller)
            button(dc, &right, by, A_BACK, TX(S_BACK), 1, 0);
        break;
    case P_WORKING: {
        y += header(dc, x0, y, w, TX(g_action ? S_WORKING_UN : S_WORKING_IN), NULL);
        y += text(dc, g_status[0] ? g_status : TX(S_WAIT), f_body, C_MUTED, x0, y, w, 0) + S(12);
        RECT tr = {x0, y, x1, y + S(8)};
        fill(dc, tr, C_FIELD);
        int seg = w / 4, pos = (g_progress * S(6)) % (w + seg) - seg;
        RECT pr = {max(x0, x0 + pos), y, min(x1, x0 + pos + seg), y + S(8)};
        if (pr.right > pr.left)
            fill(dc, pr, C_ACCENT);
        break;
    }
    case P_DONE:
        if (g_resultOk) {
            y += header(dc, x0, y, w, TX(g_action ? S_REMOVED_H : S_DONE_H), NULL);
            y += text(dc, TX(g_action ? S_REMOVED : S_DONE), f_body, C_FG, x0, y, w, 0);
            if (g_action && g_uninstaller)
                text(dc, TX(S_SELF), f_body, C_MUTED, x0, y + S(14), w, 0);
        } else {
            y += header(dc, x0, y, w, TX(S_FAIL_H), NULL);
            y += text(dc, g_status, f_body, C_ERR, x0, y, w, 0) + S(10);
            text(dc, g_detail, f_small, C_MUTED, x0, y, w, DT_EDITCONTROL);
        }
        button(dc, &right, by, A_CANCEL, TX(g_resultOk ? S_FINISH : S_CLOSE), 1, 1);
        break;
    }
}

static void set_title(void)
{
    wchar_t t[256];
    _snwprintf(t, 256, TX(g_uninstaller ? S_TITLE_UN : S_TITLE), W(VG_VERSION));
    SetWindowTextW(g_wnd, t);
}

static void show(int page)
{
    g_page = page;
    g_hover = -1;
    if (page != P_FOLDER)
        ShowWindow(g_edit, SW_HIDE);
    if (page == P_FOLDER) {
        SetWindowTextW(g_edit, g_game);
        g_folderChecked[0] = 0x1;  // force a check
        check_folder();
    }
    set_title();
    InvalidateRect(g_wnd, NULL, FALSE);
}

// ---------------------------------------------------------------- work
#define WM_APP_SAY (WM_APP + 1)
#define WM_APP_DONE (WM_APP + 2)

static void say_cb(const char *en, const char *ru)
{
    wchar_t *m = malloc(1024 * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, g_lang ? ru : en, -1, m, 1024);
    PostMessageW(g_wnd, WM_APP_SAY, 0, (LPARAM)m);
}

static DWORD WINAPI worker(LPVOID p)
{
    (void)p;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int ok = g_action ? vg_uninstall(g_game) : vg_install(g_game);
    if (!ok) {
        wchar_t *m = malloc(1024 * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, vg_last_error(), -1, m, 1024);
        PostMessageW(g_wnd, WM_APP_SAY, 1, (LPARAM)m);
    }
    PostMessageW(g_wnd, WM_APP_DONE, ok, 0);
    return 0;
}

static int is_admin(void)
{
    BOOL r = FALSE;
    SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    PSID admins;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &admins)) {
        CheckTokenMembership(NULL, admins, &r);
        FreeSid(admins);
    }
    return r;
}

static void start(int action)
{
    g_action = action;
    if (vg_game_running()) {
        if (g_page == P_FOLDER) {
            g_folderState = 3;
            InvalidateRect(g_wnd, NULL, FALSE);
        } else {
            MessageBoxW(g_wnd, TX(S_RUNNING), L"Void Gate", MB_OK | MB_ICONWARNING);
        }
        return;
    }
    if (!vg_writable(g_game)) {
        if (!is_admin()) {
            MessageBoxW(g_wnd, TX(S_ADMIN), L"Void Gate", MB_OK | MB_ICONINFORMATION);
            wchar_t me[MAX_PATH], args[MAX_PATH + 64];
            GetModuleFileNameW(NULL, me, MAX_PATH);
            _snwprintf(args, MAX_PATH + 64, L"\"%ls\" --go=%ls --lang=%ls", g_game, action ? L"uninstall" : L"install",
                       g_lang ? L"ru" : L"en");
            SHELLEXECUTEINFOW se = {sizeof se};
            se.lpVerb = L"runas", se.lpFile = me, se.lpParameters = args, se.nShow = SW_SHOWNORMAL;
            if (ShellExecuteExW(&se)) {
                DestroyWindow(g_wnd);
                return;
            }
        }
        if (g_page == P_FOLDER) {
            g_folderState = 4;
            InvalidateRect(g_wnd, NULL, FALSE);
            return;
        }
        wcscpy(g_status, TX(S_NOWRITE));
        g_detail[0] = 0;
        g_resultOk = 0;
        show(P_DONE);
        return;
    }
    if (action && vg_changed_since_install(g_game) &&
        MessageBoxW(g_wnd, TX(S_CHANGED), L"Void Gate", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
        return;
    g_status[0] = 0, g_detail[0] = 0;
    show(P_WORKING);
    SetTimer(g_wnd, 1, 30, NULL);
    CloseHandle(CreateThread(NULL, 0, worker, NULL, 0, NULL));
}

static int CALLBACK browse_cb(HWND h, UINT msg, LPARAM l, LPARAM data)
{
    (void)l;
    if (msg == BFFM_INITIALIZED && data)
        SendMessageW(h, BFFM_SETSELECTIONW, TRUE, data);
    return 0;
}

static void action(int a)
{
    switch (a) {
    case A_LANG_EN:
    case A_LANG_RU:
        g_lang = a == A_LANG_RU;
        set_title();
        break;
    case A_RADIO0:
    case A_RADIO1:
        g_choiceUninstall = a == A_RADIO1;
        break;
    case A_CANCEL:
        if (g_page != P_WORKING)
            DestroyWindow(g_wnd);
        return;
    case A_BACK:
        if (g_page == P_FOLDER)
            GetWindowTextW(g_edit, g_game, MAX_PATH);
        show(P_WELCOME);
        return;
    case A_BROWSE: {
        wchar_t cur[MAX_PATH], out[MAX_PATH];
        GetWindowTextW(g_edit, cur, MAX_PATH);
        BROWSEINFOW bi = {g_wnd};
        bi.lpszTitle = TX(S_FOLDER);
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        bi.lpfn = browse_cb, bi.lParam = (LPARAM)cur;
        LPITEMIDLIST id = SHBrowseForFolderW(&bi);
        if (id && SHGetPathFromIDListW(id, out))
            SetWindowTextW(g_edit, out);
        if (id)
            CoTaskMemFree(id);
        break;
    }
    case A_NEXT:
        if (g_page == P_WELCOME)
            show(vg_installed(g_game) && g_choiceUninstall ? P_UNINSTALL : P_FOLDER);
        else if (g_page == P_FOLDER) {
            GetWindowTextW(g_edit, g_game, MAX_PATH);
            if (vg_is_game(g_game))
                start(0);
        } else if (g_page == P_UNINSTALL)
            start(1);
        return;
    }
    InvalidateRect(g_wnd, NULL, FALSE);
}

static int hit(int x, int y)
{
    POINT p = {x, y};
    for (int i = 0; i < g_nhot; i++)
        if (PtInRect(&g_hot[i].r, p) && g_hot[i].enabled)
            return i;
    return -1;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        GetClientRect(h, &cr);
        HDC m = CreateCompatibleDC(dc);
        HBITMAP bm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        SelectObject(m, bm);
        paint(m, cr);
        BitBlt(dc, 0, 0, cr.right, cr.bottom, m, 0, 0, SRCCOPY);
        DeleteObject(bm);
        DeleteDC(m);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        int k = hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (k != g_hover) {
            g_hover = k;
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT tme = {sizeof tme, TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
        }
        SetCursor(LoadCursor(NULL, k >= 0 ? IDC_HAND : IDC_ARROW));
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hover = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_SETCURSOR:
        if ((HWND)wp == h && LOWORD(lp) == HTCLIENT)
            return TRUE;  // (set in WM_MOUSEMOVE)
        break;
    case WM_LBUTTONDOWN:
        g_pressed = hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (g_page == P_FOLDER)
            SetFocus(h);
        return 0;
    case WM_LBUTTONUP: {
        int k = hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (k >= 0 && k == g_pressed)
            action(g_hot[k].action);
        g_pressed = -1;
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
            action(A_CANCEL);
        else if (wp == VK_RETURN) {
            for (int i = 0; i < g_nhot; i++)
                if (g_hot[i].primary && g_hot[i].enabled) {
                    action(g_hot[i].action);
                    break;
                }
        }
        return 0;
    case WM_COMMAND:
        if ((HWND)lp == g_edit && HIWORD(wp) == EN_CHANGE) {
            check_folder();
            InvalidateRect(h, NULL, FALSE);
        } else if ((HWND)lp == g_edit && (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS)) {
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, RGBX(C_FG));
        SetBkColor((HDC)wp, RGBX(C_FIELD));
        return (LRESULT)b_field;
    case WM_TIMER:
        g_progress++;
        if (g_page == P_WORKING)
            InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_APP_SAY: {
        wchar_t *m = (wchar_t *)lp;
        wcsncpy(wp ? g_detail : g_status, m, 1023);  // (wp: the error behind a failure)
        free(m);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_APP_DONE:
        KillTimer(h, 1);
        g_resultOk = (int)wp;
        show(P_DONE);
        return 0;
    case WM_CLOSE:
        if (g_page == P_WORKING)
            return 0;  // never leave the game folder half-changed
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// edit box: Enter / Esc go to the window
static WNDPROC g_editProc;
static LRESULT CALLBACK edit_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        SendMessageW(g_wnd, WM_KEYDOWN, wp, lp);
        return 0;
    }
    if (msg == WM_CHAR && (wp == '\r' || wp == 27))
        return 0;
    return CallWindowProcW(g_editProc, h, msg, wp, lp);
}

static HFONT font(const wchar_t *face, int pt, int weight)
{
    return CreateFontW(-S(pt * 96.0 / 72.0), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                       face);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmdline, int show_)
{
    (void)prev, (void)cmdline, (void)show_;
    g_inst = inst;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int argc;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *hint = NULL, *go = NULL;
    for (int i = 1; i < argc; i++) {
        if (!wcsncmp(argv[i], L"--lang=", 7))
            g_lang = !wcscmp(argv[i] + 7, L"ru");
        else if (!wcsncmp(argv[i], L"--go=", 5))
            go = argv[i] + 5;
        else if (argv[i][0] != L'-')
            hint = argv[i];
    }
    wchar_t me[MAX_PATH];
    GetModuleFileNameW(NULL, me, MAX_PATH);
    const wchar_t *base = wcsrchr(me, L'\\') ? wcsrchr(me, L'\\') + 1 : me;
    wchar_t low[MAX_PATH];
    wcsncpy(low, base, MAX_PATH - 1);
    low[MAX_PATH - 1] = 0;
    _wcslwr(low);
    g_uninstaller = wcsstr(low, L"uninstall") != NULL;
    if (g_uninstaller) {
        wcscpy(g_game, me);
        *wcsrchr(g_game, L'\\') = 0;
    } else if (!vg_find_game(hint, g_game)) {
        g_game[0] = 0;
    }
    HDC sdc = GetDC(NULL);
    g_s = GetDeviceCaps(sdc, LOGPIXELSX) / 96.0;
    ReleaseDC(NULL, sdc);
    int best = 0;
    for (int i = 1; i < 5; i++)
        if (fabs(VG_UI[i].pct / 100.0 - g_s) < fabs(VG_UI[best].pct / 100.0 - g_s))
            best = i;
    g_pct = VG_UI[best].pct;
    g_side = load_png(VG_UI[best].side, &g_sideW, &g_sideH);
    g_icon = load_png(VG_UI[best].icon, &g_iconW, &g_iconH);
    f_body = font(L"Segoe UI", 10, FW_NORMAL);
    f_small = font(L"Segoe UI", 9, FW_NORMAL);
    f_btn = font(L"Segoe UI Semibold", 10, FW_SEMIBOLD);
    f_title = font(L"Segoe UI Semibold", 18, FW_SEMIBOLD);
    f_head = font(L"Segoe UI Semibold", 14, FW_SEMIBOLD);
    b_field = CreateSolidBrush(RGBX(C_FIELD));

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = wndproc, wc.hInstance = inst, wc.lpszClassName = L"VoidGateSetup";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    RegisterClassExW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT r = {0, 0, S(680), S(440)};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    g_wnd = CreateWindowExW(0, wc.lpszClassName, L"Void Gate", style, (GetSystemMetrics(SM_CXSCREEN) - ww) / 2,
                            (GetSystemMetrics(SM_CYSCREEN) - wh) / 3, ww, wh, NULL, NULL, inst, NULL);
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_wnd, 20, &dark, sizeof dark);
    COLORREF cap = RGBX(C_BG2);
    DwmSetWindowAttribute(g_wnd, 35, &cap, sizeof cap);
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 10, 10, g_wnd, NULL, inst, NULL);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)f_body, TRUE);
    SendMessageW(g_edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(6), S(6)));
    g_editProc = (WNDPROC)SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)edit_proc);
    vg_say = say_cb;
    show(g_uninstaller ? P_UNINSTALL : P_WELCOME);
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
    if (go && g_game[0])
        start(!wcscmp(go, L"uninstall"));

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
