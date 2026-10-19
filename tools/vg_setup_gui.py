"""The Void Gate setup window (tkinter): pages Welcome -> Game folder -> Installing -> Done, or the uninstaller's
Uninstall -> Removing -> Done. The work itself is tools/vg_setup.py's (passed in as S, the running module).

Dark, with the rifts' violet as the accent. Text is English by default and can be switched to
Russian at the bottom left. Sizes are given at 96 dpi and scaled to the screen; the side picture comes in five
sizes (d2mods/VoidGate/art/setup, made by tools/vg_build.py's caller once).
"""
import ctypes
import os
import queue
import subprocess
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

BG, BG2, FIELD, BORDER = "#16151c", "#1f1d27", "#24222e", "#3a3747"
FG, MUTED = "#ece9f4", "#a9a4b8"
ACCENT, ACCENT_HI = "#8a63f0", "#9d7bff"
BTN, BTN_HI = "#2b2936", "#3a3747"
OK, WARN, ERR = "#7fd18b", "#f0b44c", "#ff7a7a"

T = {
    "en": {
        "title": "Void Gate {v} Setup",
        "title_un": "Void Gate {v}: Uninstall",
        "name": "Void Gate",
        "tagline": "A Portal-style power for Dishonored 2",
        "welcome": "Open two linked rifts into the Void on any wall, floor or ceiling, step into one and come out "
                   "of the other, keeping your speed.\n\nThe setup adds Void Gate to the game on top of the mods you "
                   "already have. Close Dishonored 2 before you continue.",
        "installed": "Void Gate is already installed in this game.",
        "reinstall": "Reinstall (update)",
        "uninstall": "Uninstall",
        "folder_h": "Game folder",
        "folder": "The Dishonored 2 folder (the one with Dishonored2.exe):",
        "browse": "Browse...",
        "not_game": "This folder has no Dishonored 2 (Dishonored2.exe).",
        "build_ok": "Game version: supported. Its code matches the version the mod was made and tested on (GOG "
                    "1.77.9.0).",
        "build_other": "This version of the game has not been tested: the mod was made for GOG 1.77.9.0. You can "
                       "still install it. At every start the mod checks the game and turns itself off if it does "
                       "not fit; the game then runs normally, just without Void Gate.",
        "running": "Dishonored 2 is running. Close the game, then press the button again.",
        "un_h": "Uninstall Void Gate",
        "un_text": "Remove Void Gate from this game and put the game folder back exactly as it was before it?",
        "not_installed": "Void Gate is not installed in this game.",
        "changed": "The mod files were changed by another tool after Void Gate was installed (other mods?).\n\n"
                   "Uninstalling puts back the state from before Void Gate, so mods installed after it are removed "
                   "too; you can install them again afterwards.\n\nContinue?",
        "working_in": "Installing",
        "working_un": "Removing",
        "wait": "Please wait...",
        "done_h": "Void Gate is installed",
        "done": "Start the game, open the journal, go to Powers and buy Void Gate (4 runes). Then select it on the "
                "power wheel.\n\nHold the power button to aim and release it to open a rift. Tap F to close both "
                "rifts.\n\nTo remove the mod, run VoidGate_Uninstall.exe in the game folder.",
        "removed_h": "Void Gate is removed",
        "removed": "The game folder is as it was before Void Gate. Saves made with Void Gate still load; the power "
                   "is simply gone.",
        "fail_h": "Something went wrong",
        "admin": "The game folder can only be changed with administrator rights. Windows will ask for them now.",
        "back": "< Back", "next": "Next >", "cancel": "Cancel", "install": "Install", "finish": "Finish",
        "close": "Close",
    },
    "ru": {
        "title": "Установка «Врат Бездны» {v}",
        "title_un": "Удаление «Врат Бездны» {v}",
        "name": "Врата Бездны",
        "tagline": "Сила в стиле Portal для Dishonored 2",
        "welcome": "Откройте два связанных разлома в Бездну на любой стене, полу или потолке, войдите в один и "
                   "выйдите из другого, сохранив скорость.\n\nУстановщик добавит Врата в игру поверх уже "
                   "установленных модов. Перед продолжением закройте Dishonored 2.",
        "installed": "Врата Бездны уже установлены в этой игре.",
        "reinstall": "Переустановить (обновить)",
        "uninstall": "Удалить",
        "folder_h": "Папка игры",
        "folder": "Папка Dishonored 2 (та, где лежит Dishonored2.exe):",
        "browse": "Обзор...",
        "not_game": "В этой папке нет Dishonored 2 (Dishonored2.exe).",
        "build_ok": "Версия игры: поддерживается. Её код совпадает с версией, на которой мод сделан и проверен "
                    "(GOG 1.77.9.0).",
        "build_other": "Эта версия игры не проверялась: мод сделан для GOG 1.77.9.0. Установить всё равно можно. "
                       "При каждом запуске мод проверяет игру и, если она не подходит, выключается сам; игра тогда "
                       "работает как обычно, просто без Врат Бездны.",
        "running": "Dishonored 2 запущена. Закройте игру и нажмите кнопку ещё раз.",
        "un_h": "Удаление «Врат Бездны»",
        "un_text": "Удалить Врата Бездны из этой игры и вернуть папку игры точно такой, какой она была до них?",
        "not_installed": "Врата Бездны в этой игре не установлены.",
        "changed": "После установки Врат Бездны файлы модов менялись другой программой (другие моды?).\n\n"
                   "Удаление вернёт состояние до Врат, поэтому моды, поставленные после них, тоже пропадут; их "
                   "можно будет поставить заново.\n\nПродолжить?",
        "working_in": "Установка",
        "working_un": "Удаление",
        "wait": "Подождите...",
        "done_h": "Врата Бездны установлены",
        "done": "Запустите игру, откройте журнал, раздел «Умения», и купите Врата Бездны (4 руны). Затем выберите "
                "их на колесе сил.\n\nЗажмите кнопку силы, чтобы прицелиться, и отпустите, чтобы открыть разлом. "
                "Короткое нажатие F закрывает оба разлома.\n\nЧтобы удалить мод, запустите VoidGate_Uninstall.exe "
                "в папке игры.",
        "removed_h": "Врата Бездны удалены",
        "removed": "Папка игры такая же, как до установки Врат Бездны. Сохранения с Вратами загружаются, сила из "
                   "них просто пропадает.",
        "fail_h": "Что-то пошло не так",
        "admin": "Папку игры можно изменить только с правами администратора. Сейчас Windows их запросит.",
        "back": "< Назад", "next": "Далее >", "cancel": "Отмена", "install": "Установить", "finish": "Готово",
        "close": "Закрыть",
    },
}
LANGS = [("en", "English"), ("ru", "Русский")]


class App:
    def __init__(self, S, game_arg):
        self.S = S
        self.uninstaller = "uninstall" in os.path.basename(sys.argv[0]).lower()
        self.go = next((a[5:] for a in sys.argv if a.startswith("--go=")), None)  # after an elevated restart
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:  # noqa: BLE001
            pass
        self.root = root = tk.Tk()
        s = root.winfo_fpixels("1i") / 96.0
        self.pct = min((100, 125, 150, 175, 200), key=lambda p: abs(p / 100 - s))
        self.s = s
        self.ui = os.path.join(S.PAYLOAD, "ui")
        try:
            root.iconbitmap(os.path.join(self.ui, "voidgate.ico"))
        except tk.TclError:
            pass
        root.configure(bg=BG)
        root.resizable(False, False)
        w, h = self.px(680), self.px(440)
        root.geometry(f"{w}x{h}+{(root.winfo_screenwidth() - w) // 2}+{(root.winfo_screenheight() - h) // 3}")
        self.style()
        self.dark_titlebar()
        self.side = tk.PhotoImage(file=os.path.join(self.ui, f"side_{self.pct}.png"))
        self.icon = tk.PhotoImage(file=os.path.join(self.ui, f"icon_{self.pct}.png"))
        tk.Label(root, image=self.side, bd=0, bg=BG).pack(side="left", fill="y")
        right = ttk.Frame(root)
        right.pack(side="left", fill="both", expand=True)
        self.content = ttk.Frame(right, padding=(self.px(28), self.px(22), self.px(26), self.px(8)))
        self.content.pack(fill="both", expand=True)
        tk.Frame(right, bg=BORDER, height=1).pack(fill="x")
        bar = ttk.Frame(right, style="Bar.TFrame", padding=(self.px(14), self.px(10)))
        bar.pack(fill="x")
        self.lang = tk.StringVar(value=dict(LANGS)[S.LANG])
        lang_box = ttk.Combobox(bar, textvariable=self.lang, values=[n for _, n in LANGS], state="readonly",
                                width=9, style="Bar.TCombobox")
        lang_box.pack(side="left")
        lang_box.bind("<<ComboboxSelected>>", self.set_lang)
        self.b_cancel = ttk.Button(bar)
        self.b_next = ttk.Button(bar, style="Accent.TButton")
        self.b_back = ttk.Button(bar)
        root.protocol("WM_DELETE_WINDOW", self.quit)

        self.game = S.find_game(game_arg) if not self.uninstaller else os.path.dirname(os.path.abspath(sys.argv[0]))
        self.folder = tk.StringVar(value=self.game or "")
        self.choice = tk.StringVar(value="reinstall")
        self.build_cache = {}
        self.q = queue.Queue()
        self.result = None  # (ok, last message) of the last run
        self.page = None
        if self.go and self.game:
            self.start(self.go)
        else:
            self.show("uninstall" if self.uninstaller else "welcome")
        root.after(100, self.poll)

    # ------------------------------------------------------------------ look
    def px(self, v):
        return int(round(v * self.s))

    def t(self, key):
        return T[self.S.LANG][key]

    def dark_titlebar(self):
        """Windows 10/11: a dark title bar, on Windows 11 in the window's own colour (DWM attributes 20 and 35)."""
        try:
            self.root.update_idletasks()
            hwnd = ctypes.windll.user32.GetParent(self.root.winfo_id())
            for attr, val in ((20, 1), (35, int(BG2[5:7] + BG2[3:5] + BG2[1:3], 16))):
                v = ctypes.c_int(val)
                ctypes.windll.dwmapi.DwmSetWindowAttribute(hwnd, attr, ctypes.byref(v), ctypes.sizeof(v))
        except Exception:  # noqa: BLE001 - older Windows: the usual title bar
            pass

    def style(self):
        st = ttk.Style(self.root)
        st.theme_use("clam")
        base = ("Segoe UI", 10)
        st.configure(".", background=BG, foreground=FG, font=base, bordercolor=BORDER, focuscolor=ACCENT,
                     troughcolor=FIELD, selectbackground=ACCENT, selectforeground="#ffffff")
        st.configure("TFrame", background=BG)
        st.configure("Bar.TFrame", background=BG2)
        st.configure("TLabel", background=BG, foreground=FG)
        st.configure("Title.TLabel", font=("Segoe UI Semibold", 18))
        st.configure("Head.TLabel", font=("Segoe UI Semibold", 14))
        st.configure("Tag.TLabel", foreground=MUTED, font=("Segoe UI", 10))
        st.configure("Ok.TLabel", foreground=OK)
        st.configure("Warn.TLabel", foreground=WARN)
        st.configure("Err.TLabel", foreground=ERR)
        st.configure("Muted.TLabel", foreground=MUTED)
        pad = (self.px(16), self.px(5))
        st.configure("TButton", background=BTN, foreground=FG, lightcolor=BTN, darkcolor=BTN, bordercolor=BORDER,
                     padding=pad, relief="flat")
        st.map("TButton", background=[("disabled", BG2), ("pressed", BORDER), ("active", BTN_HI)],
               foreground=[("disabled", MUTED)], lightcolor=[("active", BTN_HI)], darkcolor=[("active", BTN_HI)])
        st.configure("Accent.TButton", background=ACCENT, foreground="#ffffff", lightcolor=ACCENT, darkcolor=ACCENT,
                     bordercolor=ACCENT, font=("Segoe UI Semibold", 10))
        st.map("Accent.TButton", background=[("disabled", BG2), ("pressed", ACCENT), ("active", ACCENT_HI)],
               lightcolor=[("active", ACCENT_HI)], darkcolor=[("active", ACCENT_HI)],
               bordercolor=[("disabled", BORDER)], foreground=[("disabled", MUTED)])
        st.configure("TEntry", fieldbackground=FIELD, foreground=FG, insertcolor=FG, lightcolor=FIELD,
                     darkcolor=FIELD, bordercolor=BORDER, padding=self.px(5))
        st.map("TEntry", bordercolor=[("focus", ACCENT)], lightcolor=[("focus", ACCENT)])
        st.configure("TRadiobutton", background=BG, foreground=FG, indicatorbackground=FIELD,
                     indicatorforeground=FIELD, upperbordercolor=MUTED, lowerbordercolor=MUTED,
                     indicatorsize=self.px(13), indicatormargin=(0, 0, self.px(8), 0), padding=(0, self.px(4)))
        st.map("TRadiobutton", indicatorbackground=[("selected", ACCENT)], background=[("active", BG)],
               upperbordercolor=[("selected", ACCENT_HI)], lowerbordercolor=[("selected", ACCENT_HI)])
        st.configure("Horizontal.TProgressbar", troughcolor=FIELD, background=ACCENT, lightcolor=ACCENT,
                     darkcolor=ACCENT, bordercolor=BORDER, thickness=self.px(8))
        st.configure("Bar.TCombobox", fieldbackground=BTN, background=BTN, foreground=FG, arrowcolor=FG,
                     bordercolor=BORDER, lightcolor=BTN, darkcolor=BTN, padding=self.px(3))
        st.map("Bar.TCombobox", fieldbackground=[("readonly", BTN)], foreground=[("readonly", FG)],
               selectbackground=[("readonly", BTN)], selectforeground=[("readonly", FG)])
        self.root.option_add("*TCombobox*Listbox.background", FIELD)
        self.root.option_add("*TCombobox*Listbox.foreground", FG)
        self.root.option_add("*TCombobox*Listbox.selectBackground", ACCENT)
        self.root.option_add("*TCombobox*Listbox.font", base)

    def wrap(self):
        return self.px(680 - 200 - 28 - 26 - 4)

    def text(self, parent, txt, style="TLabel", pady=(0, 0)):
        lab = ttk.Label(parent, text=txt, style=style, wraplength=self.wrap(), justify="left")
        lab.pack(anchor="w", pady=pady)
        return lab

    def header(self, title, sub=None):
        row = ttk.Frame(self.content)
        row.pack(anchor="w", fill="x", pady=(0, self.px(14)))
        tk.Label(row, image=self.icon, bg=BG, bd=0).pack(side="left", padx=(0, self.px(12)))
        col = ttk.Frame(row)
        col.pack(side="left", anchor="w")
        ttk.Label(col, text=title, style="Title.TLabel" if sub else "Head.TLabel").pack(anchor="w")
        if sub:
            ttk.Label(col, text=sub, style="Tag.TLabel").pack(anchor="w")

    def buttons(self, back=None, next_=None, cancel=None, last=False):
        """Back is hidden when None; Next / Cancel are greyed out when None, and Cancel is hidden on the last page.
        Packed from the right: Cancel, then Next, then Back (so Back stays left of Next)."""
        for b in (self.b_back, self.b_next, self.b_cancel):
            b.pack_forget()
        if not last:
            self.b_cancel.pack(side="right")
        self.b_next.pack(side="right", padx=(0, 0 if last else self.px(8)))
        if back:
            self.b_back.pack(side="right", padx=(0, self.px(6)))
        for b, spec in ((self.b_back, back), (self.b_next, next_), (self.b_cancel, cancel)):
            if spec is None:
                b.configure(state="disabled")
                continue
            label, cmd = spec
            b.configure(text=self.t(label), command=cmd, state="normal")

    # ------------------------------------------------------------------ pages
    def show(self, page):
        self.page = page
        self.root.title(self.t("title_un" if self.uninstaller else "title").format(v=self.S.VERSION))
        for w in self.content.winfo_children():
            w.destroy()
        getattr(self, "page_" + page)()

    def set_lang(self, _=None):
        self.S.LANG = next(c for c, n in LANGS if n == self.lang.get())
        self.show(self.page)

    def page_welcome(self):
        self.header(self.t("name"), self.t("tagline"))
        self.text(self.content, self.t("welcome"))
        installed = self.game and self.S.load_state(self.game) is not None
        if installed:
            self.text(self.content, self.t("installed"), "Ok.TLabel", pady=(self.px(14), self.px(4)))
            for val in ("reinstall", "uninstall"):
                ttk.Radiobutton(self.content, text=self.t(val), value=val, variable=self.choice).pack(anchor="w")
        self.buttons(None, ("next", self.welcome_next), ("cancel", self.quit))

    def welcome_next(self):
        installed = self.game and self.S.load_state(self.game) is not None
        self.show("uninstall" if installed and self.choice.get() == "uninstall" else "folder")

    def page_folder(self):
        self.header(self.t("folder_h"))
        self.text(self.content, self.t("folder"), pady=(0, self.px(6)))
        row = ttk.Frame(self.content)
        row.pack(fill="x")
        entry = ttk.Entry(row, textvariable=self.folder)
        entry.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text=self.t("browse"), command=self.browse).pack(side="left", padx=(self.px(8), 0))
        self.status = self.text(self.content, "", pady=(self.px(12), 0))
        self.folder.trace_add("write", lambda *_: self.check_folder())
        self.buttons(("back", lambda: self.show("welcome")), ("install", self.install), ("cancel", self.quit))
        self.check_folder()

    def browse(self):
        d = filedialog.askdirectory(parent=self.root, initialdir=self.folder.get() or "C:\\",
                                    title=self.t("folder_h"))
        if d:
            self.folder.set(os.path.normpath(d))

    def check_folder(self):
        if self.page != "folder":
            return False
        d = self.folder.get().strip().strip('"')
        if not self.S.is_game(d):
            self.status.configure(text=self.t("not_game"), style="Err.TLabel")
            self.b_next.configure(state="disabled")
            return False
        d = os.path.abspath(d)
        if d not in self.build_cache:
            try:
                self.build_cache[d] = self.S.check_build(d)[0]
            except Exception:  # noqa: BLE001
                self.build_cache[d] = False
        ok = self.build_cache[d]
        self.status.configure(text=("✓  " if ok else "⚠  ") + self.t("build_ok" if ok else "build_other"),
                              style="Ok.TLabel" if ok else "Warn.TLabel")
        self.b_next.configure(state="normal")
        return True

    def install(self):
        if not self.check_folder():
            return
        self.game = os.path.abspath(self.folder.get().strip().strip('"'))
        self.start("install")

    def page_uninstall(self):
        self.header(self.t("un_h"))
        if not self.game or self.S.load_state(self.game) is None:
            self.text(self.content, self.t("not_installed"), "Err.TLabel")
            self.buttons(None, ("close", self.quit), None, last=True)
            return
        self.text(self.content, self.t("un_text"))
        self.text(self.content, self.game, "Muted.TLabel", pady=(self.px(12), 0))
        back = None if self.uninstaller else ("back", lambda: self.show("welcome"))
        self.buttons(back, ("uninstall", lambda: self.start("uninstall")), ("cancel", self.quit))

    def page_working(self):
        self.header(self.t("working_un" if self.action == "uninstall" else "working_in"))
        self.status = self.text(self.content, self.t("wait"), "Muted.TLabel", pady=(0, self.px(12)))
        bar = ttk.Progressbar(self.content, mode="indeterminate", length=self.wrap())
        bar.pack(anchor="w")
        bar.start(12)
        self.buttons(None, None, None)
        self.b_next.configure(text=self.t("next"))
        self.b_cancel.configure(text=self.t("cancel"))

    def page_done(self):
        ok, msg = self.result
        if ok:
            un = self.action == "uninstall"
            self.header(self.t("removed_h" if un else "done_h"))
            self.text(self.content, self.t("removed" if un else "done"))
        else:
            self.header(self.t("fail_h"))
            self.text(self.content, msg or "", "Err.TLabel")
        self.buttons(None, ("finish" if ok else "close", self.quit), None, last=True)

    # ------------------------------------------------------------------ work
    def rights(self, action):
        """True when the game folder can be written; otherwise restart elevated (UAC) to do `action` there."""
        if self.S.writable(self.game):
            return True
        try:
            admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
        except Exception:  # noqa: BLE001
            admin = False
        if not admin:
            messagebox.showinfo(self.root.title(), self.t("admin"), parent=self.root)
            frozen = getattr(sys, "frozen", False)
            args = ([] if frozen else [os.path.abspath(self.S.__file__)]) + \
                [self.game, f"--go={action}", f"--lang={self.S.LANG}"]
            if ctypes.windll.shell32.ShellExecuteW(None, "runas", sys.executable, subprocess.list2cmdline(args),
                                                   None, 1) > 32:
                self.root.destroy()
                return False
        self.result = (False, T[self.S.LANG]["admin"])
        self.action = action
        self.show("done")
        return False

    def start(self, action):
        self.action = action
        if self.S.game_running():
            if self.page == "folder":
                self.status.configure(text=self.t("running"), style="Err.TLabel")
            else:
                messagebox.showwarning(self.root.title(), self.t("running"), parent=self.root)
                if self.page is None:
                    self.show("uninstall" if action == "uninstall" else "folder")
            return
        if not self.rights(action):
            return
        if action == "uninstall":
            state = self.S.load_state(self.game)
            if state and self.S.changed_since_install(self.game, state) and not messagebox.askyesno(
                    self.root.title(), self.t("changed"), icon="warning", parent=self.root):
                return
        self.show("working")
        self.last = ""

        def work():
            self.S.SAY = lambda m: self.q.put(("say", m))
            try:
                ok = self.S.install(self.game, yes=True) if action == "install" else \
                    self.S.uninstall(self.game, yes=True)
            except Exception as e:  # noqa: BLE001
                ok, self.last = False, f"{type(e).__name__}: {e}"
            self.q.put(("done", ok))

        threading.Thread(target=work, daemon=True).start()

    def poll(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "say":
                    self.last = val.strip()
                    if self.page == "working":
                        self.status.configure(text=self.last)
                else:
                    self.result = (val, self.last)
                    self.show("done")
        except queue.Empty:
            pass
        self.root.after(100, self.poll)

    def quit(self):
        if self.page == "working":
            return  # never leave the game folder half-changed
        self.root.destroy()


def run(S, game_arg=None):
    app = App(S, game_arg)
    app.root.mainloop()
    if S.SELF_DELETE:  # the uninstaller in the game folder removes itself once this process has exited
        subprocess.Popen(f'cmd /c ping -n 4 127.0.0.1 >nul & del /f /q "{S.SELF_DELETE}"',
                         creationflags=0x08000000 | 0x00000008)
