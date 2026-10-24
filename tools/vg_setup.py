"""Void Gate setup, the Python REFERENCE: installs / uninstalls the mod in a Dishonored 2 folder (console).
Players get the native setup (native_d2/setup, the same logic in C); tests compare the two.

The data goes into the game's mod catalogs (base/gameN_voidmod.index + .resources, master.index -- the layout Void
Installer writes too), ON TOP of whatever mods are already there: their index entries and data are carried over, ours
are added. The game folder's state before the first install is kept in base/voidgate_backup/, and Uninstall puts
exactly that back. The native part (dinput8.dll + voidgate.dll) checks the game's code at every start and stays off
on a build it was not made for.

Run from a folder with payload/ (mod data, bin/*.dll, sig.json) -- or the one-file exe built by tools/vg_build.py.
  VoidGateSetup.exe [game folder]
"""
import ctypes
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

APP, VERSION = "Void Gate", "0.9.2"
CATALOGS = (1, 2, 3, 4)
CONTAINERS = ["", "_001", "_002", "_003", "_004", "_005", "_patch", "_voidmod"]
VOIDMOD = 7
FLAG_FILE = 0x20
UNINSTALLER = "VoidGate_Uninstall.exe"
SELF_DELETE = None
OUR_MARK = b"Void Gate mod: this build"  # a string in our dinput8.dll (its unsupported-build message)


def here():
    return getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))


PAYLOAD = os.path.join(here(), "payload")


LANG = "en"  # English unless --lang=ru or the window's language switch says otherwise
SAY = None  # the window's message hook (vg_setup_gui); None = print to the console


def say(en, ru=None):
    text = ru if LANG == "ru" and ru else en
    if SAY:
        SAY(text)
    else:
        print(text)


# ---------------------------------------------------------------- index files (same format as tools/d2idx.py)
def read_index(path):
    d = open(path, "rb").read()
    n = struct.unpack_from(">I", d, 0x20)[0]
    p, out = 0x24, []
    for _ in range(n):
        e = {"id": struct.unpack_from(">I", d, p)[0]}
        p += 4
        for k in ("type", "name", "path"):
            ln = struct.unpack_from("<I", d, p)[0]
            e[k] = d[p + 4:p + 4 + ln].decode("latin1")
            p += 4 + ln
        e["off"], e["size"], e["zsize"] = struct.unpack_from(">QII", d, p)
        e["tail"] = bytearray(d[p + 16:p + 26])
        p += 26
        out.append(e)
    return out


def write_index(path, entries):
    body = bytearray(struct.pack(">I", len(entries)))
    for e in entries:
        body += struct.pack(">I", e["id"])
        for k in ("type", "name", "path"):
            s = e[k].encode("latin1")
            body += struct.pack("<I", len(s)) + s
        body += struct.pack(">QII", e["off"], e["size"], e["zsize"]) + bytes(e["tail"])
    open(path, "wb").write(b"\x05SER" + struct.pack(">I", len(body)) + bytes(24) + bytes(body))


def master_index(stock_master):
    """master.index pointing the game at the voidmod catalogs (the layout Void Installer writes)."""
    out = bytearray(b"\x04SER" + struct.pack("<HH", 0x200, 0x400))
    for g in CATALOGS:
        for s in (f"game{g}_voidmod.index", f"game{g}.resources"):
            out += struct.pack("<I", len(s)) + s.encode()
    extras = [(f"game{g}{suf}.resources", g - 1) for suf in CONTAINERS[1:] for g in CATALOGS]
    out += struct.pack(">I", len(extras))
    for name, c in extras:
        out += struct.pack("<I", len(name)) + name.encode() + struct.pack(">H", c)
    tail = stock_master[stock_master.rindex(b"\x00\x00\x00\x01\x00\x00\x00\x02"):]
    return bytes(out + tail)


def compress(data):
    c = zlib.compressobj(1, zlib.DEFLATED, 10)
    z = c.compress(data) + c.flush()
    return z if len(z) < len(data) else data


# ---------------------------------------------------------------- the game folder
def game_running():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Dishonored2.exe"], capture_output=True, text=True,
                         creationflags=0x08000000).stdout
    return "Dishonored2.exe" in out


def is_game(d):
    return d and os.path.isfile(os.path.join(d, "Dishonored2.exe")) and \
        os.path.isfile(os.path.join(d, "base", "game1_patch.index"))


def find_game(arg=None):
    here_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
    cands = [arg, os.getcwd(), here_dir, os.path.dirname(here_dir)]
    for drive in "CDEFGH":
        cands += [rf"{drive}:\GOG Games\Dishonored 2", rf"{drive}:\Games\Dishonored 2", rf"{drive}:\Dishonored 2",
                  rf"{drive}:\Program Files (x86)\GOG Galaxy\Games\Dishonored 2"]
    try:
        import winreg
        for gog in (r"SOFTWARE\WOW6432Node\GOG.com\Games", r"SOFTWARE\GOG.com\Games"):  # any GOG game folder
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, gog) as k:
                    for i in range(winreg.QueryInfoKey(k)[0]):
                        try:
                            with winreg.OpenKey(k, winreg.EnumKey(k, i)) as g:
                                cands.append(winreg.QueryValueEx(g, "path")[0])
                        except OSError:
                            pass
            except OSError:
                pass
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as k:
                steam = winreg.QueryValueEx(k, "SteamPath")[0]
            libs = [steam]
            vdf = os.path.join(steam, "steamapps", "libraryfolders.vdf")
            if os.path.isfile(vdf):
                libs += re.findall(r'"path"\s+"([^"]+)"', open(vdf, encoding="utf-8", errors="ignore").read())
            cands += [os.path.join(lib.replace("\\\\", "\\"), "steamapps", "common", "Dishonored2") for lib in libs]
        except OSError:
            pass
    except ImportError:
        pass
    for c in cands:
        if c and is_game(c):
            return os.path.abspath(c)
    return None


def check_build(game):
    """(supported?, mismatches, checks): the bytes the mod expects at every game address it uses."""
    sig = json.load(open(os.path.join(PAYLOAD, "sig.json")))
    d = open(os.path.join(game, "Dishonored2.exe"), "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec, optsz = struct.unpack_from("<H", d, pe + 6)[0], struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<Q", d, pe + 24 + 24)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + 40 * i
        vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
        secs.append((base + va, min(vsz, rsz), raw))
    bad = 0
    for va, hexbytes in sig["checks"]:
        want = bytes.fromhex(hexbytes)
        got = None
        for sva, size, raw in secs:
            if sva <= va and va + len(want) <= sva + size:
                got = d[raw + va - sva:raw + va - sva + len(want)]
        bad += got != want
    return bad == 0, bad, len(sig["checks"])


# ---------------------------------------------------------------- building the catalogs
class Catalog:
    """One catalog's current index (voidmod if mods are installed, else the stock patch index) and data files."""

    def __init__(self, base, g):
        self.base, self.g = base, g
        vm = os.path.join(base, f"game{g}_voidmod.index")
        self.has_voidmod = os.path.isfile(vm)
        self.idx = read_index(vm if self.has_voidmod else os.path.join(base, f"game{g}_patch.index"))
        self.by_path = {}
        for i, e in enumerate(self.idx):
            self.by_path.setdefault(e["path"], []).append(i)

    def read(self, i):
        e = self.idx[i]
        with open(os.path.join(self.base, f"game{self.g}{CONTAINERS[e['tail'][9] >> 2]}.resources"), "rb") as f:
            f.seek(e["off"])
            return f.read(e["zsize"])

    def data(self, i):
        raw, e = self.read(i), self.idx[i]
        return zlib.decompress(raw) if e["zsize"] != e["size"] else raw


def merge_lang(data, entries):
    lines = data.decode("utf-8").split("\n")
    pos = {}
    for i, ln in enumerate(lines):
        m = re.match(r'\t"([^"]+)"\t', ln)
        if m:
            pos[m.group(1)] = i
    close = max(i for i, ln in enumerate(lines) if ln.strip() == "}")
    added = []
    for k, v in entries.items():
        ln = f'\t"{k}"\t"{v}"'
        if k in pos:
            lines[pos[k]] = ln
        else:
            added.append(ln)
    lines[close:close] = added
    return "\n".join(lines).encode("utf-8")


def mod_plan(cats, mod):
    info = json.load(open(os.path.join(mod, "mod.json"), encoding="utf-8"))
    files = {}
    root = os.path.join(mod, "files")
    for dp, _, fns in os.walk(root):
        for fn in fns:
            full = os.path.join(dp, fn)
            files[os.path.relpath(full, root).replace(os.sep, "/")] = open(full, "rb").read()

    def game_file(path):
        for c in cats.values():
            if path in c.by_path:
                return c.data(c.by_path[path][0])
        raise RuntimeError(f"{path} is not in this game's data")

    # stock resources another catalog needs too (the portal model of the Dunwall tower, its sounds...): taken from
    # the player's own game, not shipped with the mod
    for path, ent in info.get("entries", {}).items():
        if ent.get("copy") and path not in files:
            files[path] = game_file(path)
    langdir = os.path.join(mod, "lang")
    for fn in sorted(os.listdir(langdir)) if os.path.isdir(langdir) else []:
        path = f"strings/{fn[:-5]}.lang"  # the lang file as it is now (stock or another mod's), our keys on top
        files[path] = merge_lang(game_file(path), json.load(open(os.path.join(langdir, fn), encoding="utf-8")))
    dir_type = {}
    for c in cats.values():
        for e in c.idx:
            m = re.match(r"generated/decls/([^/]+)/", e["path"])
            if m:
                dir_type.setdefault(m.group(1), e["type"])
    per = {g: {"replace": {}, "new": []} for g in CATALOGS}
    for path, data in sorted(files.items()):
        gs = [g for g in CATALOGS if path in cats[g].by_path]
        ent = info.get("entries", {}).get(path, {})
        if ent.get("copy"):
            for g in info.get("new_catalogs", [1]):
                if g not in gs:
                    per[g]["new"].append(dict(path=path, type=ent["type"], name=ent["name"],
                                              tail=bytes.fromhex(ent["tail"]), data=data))
            continue
        if gs:
            for g in gs:
                per[g]["replace"][path] = data
            continue
        m = re.match(r"generated/decls/([^/]+)/(.+)\.decl$", path)
        if m:
            typ, name, flag = dir_type[m.group(1)], m.group(2), 0
        elif re.match(r"generated/image/(.+)\.bimage7$", path):
            typ, name, flag = "image", path[len("generated/image/"):-len(".bimage7")] + ".png", FLAG_FILE
        else:
            typ, name, flag = "File", path, FLAG_FILE
        typ, name = ent.get("type", typ), ent.get("name", name)
        tail = bytes.fromhex(ent["tail"]) if "tail" in ent else None
        for g in info.get("new_catalogs", [1]):
            per[g]["new"].append(dict(path=path, type=typ, name=name, flag=flag, tail=tail, data=data))
    return per


def order_bad(entries):
    """Indexes i where entries i-1 and i share a data file but the offset goes back."""
    return {i for i in range(1, len(entries))
            if entries[i - 1]["tail"][9] == entries[i]["tail"][9] and entries[i - 1]["off"] > entries[i]["off"]}


def build_catalog(c, plan, out_dir):
    """Write game<g>_voidmod.index/.resources: every entry whose data lives in a voidmod file (another mod's, or one
    of ours) gets its data re-written in index order (the engine's loader wants non-decreasing offsets per file)."""
    g = c.g
    entries = [dict(e, tail=bytearray(e["tail"])) for e in c.idx]
    ours = {}
    for path, data in plan["replace"].items():
        for i in c.by_path[path]:
            ours[i] = compress(data), len(data)
    blob = bytearray(b"\x04SER")
    for i, e in enumerate(entries):
        if i in ours:
            z, size = ours[i]
        elif (e["tail"][9] >> 2) == VOIDMOD:
            z, size = c.read(i), e["size"]  # another mod's data: carried over as it is
        else:
            continue
        e["off"], e["size"], e["zsize"] = len(blob), size, len(z)
        e["tail"][9] = (VOIDMOD << 2) | (g - 1)
        blob += z
    next_id = max((e["id"] for e in entries), default=-1) + 1
    have = {e["path"] for e in entries}
    for n in plan["new"]:
        if n["path"] in have:
            continue
        z = compress(n["data"])
        tail = bytearray(n["tail"]) if n.get("tail") else bytearray(10)
        if not n.get("tail"):
            tail[7] = n.get("flag", 0)
        tail[9] = (VOIDMOD << 2) | (g - 1)
        entries.append(dict(id=next_id, type=n["type"], name=n["name"], path=n["path"], off=len(blob),
                            size=len(n["data"]), zsize=len(z), tail=tail))
        next_id += 1
        blob += z
    if order_bad(entries) - order_bad(c.idx):
        raise RuntimeError(f"game{g}: the index order would break the game's background loader")
    write_index(os.path.join(out_dir, f"game{g}_voidmod.index"), entries)
    open(os.path.join(out_dir, f"game{g}_voidmod.resources"), "wb").write(blob)


# ---------------------------------------------------------------- install / uninstall
MOD_FILES = ["master.index"] + [f"game{g}_voidmod.{x}" for g in CATALOGS for x in ("index", "resources")]


def backup_dir(game):
    return os.path.join(game, "base", "voidgate_backup")


def ours(path):
    try:
        return OUR_MARK in open(path, "rb").read()
    except OSError:
        return False


def file_hash(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def current_hashes(game):
    base = os.path.join(game, "base")
    return {fn: file_hash(os.path.join(base, fn)) for fn in MOD_FILES if os.path.isfile(os.path.join(base, fn))}


def load_state(game):
    try:
        return json.load(open(os.path.join(backup_dir(game), "state.json")))
    except (OSError, ValueError):
        return None


def changed_since_install(game, state):
    """True when another tool (Void Installer, say) rewrote the mod catalogs after our install."""
    return current_hashes(game) != state.get("wrote", {})


def make_backup(game):
    b, base = backup_dir(game), os.path.join(game, "base")
    os.makedirs(b)
    state = {"made": datetime.datetime.now().isoformat(timespec="seconds"), "version": VERSION, "files": []}
    for fn in MOD_FILES:
        if os.path.isfile(os.path.join(base, fn)):
            shutil.copy2(os.path.join(base, fn), b)
            state["files"].append(fn)
    dll = os.path.join(game, "dinput8.dll")
    if os.path.isfile(dll) and not ours(dll):
        shutil.copy2(dll, os.path.join(b, "dinput8.dll.other"))
        state["other_dinput8"] = True
    json.dump(state, open(os.path.join(b, "state.json"), "w"), indent=1)
    return state


def remove_native(game, state):
    global SELF_DELETE
    for fn in ("voidgate.dll", "dmod_d2.log", "dmod_cmd.txt", "dmod_reply.txt"):
        p = os.path.join(game, fn)
        if os.path.isfile(p):
            os.remove(p)
    for fn in os.listdir(game):
        if fn.startswith("voidgate.live") and fn.endswith(".dll"):
            try:
                os.remove(os.path.join(game, fn))
            except OSError:
                pass
    dll = os.path.join(game, "dinput8.dll")
    if state.get("other_dinput8"):
        shutil.copy2(os.path.join(backup_dir(game), "dinput8.dll.other"), dll)
    elif os.path.isfile(dll) and ours(dll):
        os.remove(dll)
    un = os.path.join(game, UNINSTALLER)
    if os.path.isfile(un):
        if running_exe() and os.path.normcase(os.path.abspath(un)) == os.path.normcase(running_exe()):
            SELF_DELETE = un  # (deleted after this process exits)
        else:
            os.remove(un)


def running_exe():
    return os.path.abspath(sys.executable) if getattr(sys, "frozen", False) else None


def place_uninstaller(game):
    """A copy of this setup in the game folder that, by its name, does nothing but uninstall."""
    exe, un = running_exe(), os.path.join(game, UNINSTALLER)
    if exe and os.path.normcase(exe) != os.path.normcase(os.path.abspath(un)):
        shutil.copy2(exe, un)


def restore_backup(game, state):
    b, base = backup_dir(game), os.path.join(game, "base")
    for fn in MOD_FILES:
        p = os.path.join(base, fn)
        if fn in state["files"]:
            shutil.copy2(os.path.join(b, fn), p)
        elif os.path.isfile(p):
            os.remove(p)
    remove_native(game, state)


def writable(game):
    p = os.path.join(game, "base", "voidgate_write_test.tmp")
    try:
        open(p, "wb").close()
        os.remove(p)
        return True
    except OSError:
        return False


def need_rights(game, args, yes):
    """A game in Program Files can only be changed with administrator rights: ask Windows for them (UAC) and run
    again elevated with the same choices; this process then ends."""
    if writable(game):
        return
    try:
        admin = bool(ctypes.windll.shell32.IsUserAnAdmin())
    except Exception:  # noqa: BLE001
        admin = False
    if not admin:
        say("\nThe game folder can only be changed with administrator rights: Windows will ask for them.",
            "Папку игры можно изменить только с правами администратора: Windows сейчас их запросит.")
        exe = sys.executable
        params = subprocess.list2cmdline(([] if getattr(sys, "frozen", False) else [os.path.abspath(__file__)]) +
                                         args + (["--yes"] if yes else []))
        if ctypes.windll.shell32.ShellExecuteW(None, "runas", exe, params, None, 1) > 32:
            sys.exit(0)
    say("\nCannot write to the game folder. Close the game and anything that uses its files, then try again.",
        "Не удаётся записать в папку игры. Закройте игру и всё, что использует её файлы, и попробуйте снова.")
    if not yes:
        input("\nEnter - exit / выход")
    sys.exit(1)


def ask(en, ru, yes):
    if yes:
        return True
    say(en, ru)
    return input("y/n (д/н) > ").strip().lower() in ("y", "yes", "д", "да")


def install(game, yes=False):
    if game_running():
        say("Close Dishonored 2 first.", "Сначала закройте Dishonored 2.")
        return False
    base = os.path.join(game, "base")
    state = load_state(game)
    if state:
        if changed_since_install(game, state):
            # other mods were (re)installed over ours: build on what is there now, keep the original backup
            say("The mod catalogs changed since Void Gate was installed (other mods?): adding Void Gate to them.",
                "Каталоги модов изменились после установки Врат Бездны (другие моды?): добавляю Врата к ним.")
        else:
            say("Void Gate is already installed: reinstalling.", "Врата Бездны уже установлены: переустанавливаю.")
            restore_backup(game, state)
    else:
        state = make_backup(game)
    tmp = os.path.join(base, "voidgate_tmp")
    try:
        cats = {g: Catalog(base, g) for g in CATALOGS}
        plan = mod_plan(cats, os.path.join(PAYLOAD, "VoidGate"))
        shutil.rmtree(tmp, ignore_errors=True)
        os.makedirs(tmp)
        for g in CATALOGS:
            build_catalog(cats[g], plan[g], tmp)
        cur = open(os.path.join(base, "master.index"), "rb").read()
        if b"game1_voidmod.index" not in cur:
            open(os.path.join(tmp, "master.index"), "wb").write(master_index(cur))
        for fn in os.listdir(tmp):
            os.replace(os.path.join(tmp, fn), os.path.join(base, fn))
        os.rmdir(tmp)
        for fn in ("dinput8.dll", "voidgate.dll"):
            shutil.copy2(os.path.join(PAYLOAD, "bin", fn), os.path.join(game, fn))
        place_uninstaller(game)
    except Exception as e:  # noqa: BLE001
        say(f"Install failed ({e}): putting the game folder back as it was.",
            f"Установка не удалась ({e}): возвращаю папку игры как было.")
        shutil.rmtree(tmp, ignore_errors=True)
        restore_backup(game, state)
        shutil.rmtree(backup_dir(game), ignore_errors=True)
        return False
    state["wrote"] = current_hashes(game)
    json.dump(state, open(os.path.join(backup_dir(game), "state.json"), "w"), indent=1)
    say("\nInstalled. In the game: journal -> Powers -> Void Gate (buy it with runes).",
        "Установлено. В игре: журнал -> Умения -> Врата Бездны (покупаются за руны).")
    return True


def uninstall(game, yes=False):
    if game_running():
        say("Close Dishonored 2 first.", "Сначала закройте Dishonored 2.")
        return False
    state = load_state(game)
    if not state:
        say("Void Gate is not installed here.", "Врата Бездны здесь не установлены.")
        return False
    if changed_since_install(game, state) and not ask(
            "The mod catalogs were changed by another tool after Void Gate was installed. Uninstalling puts back the"
            " state from before Void Gate, so mods installed after it are removed too (install them again after)."
            " Continue?",
            "Каталоги модов менялись после установки Врат Бездны. Удаление вернёт состояние до Врат, поэтому моды,"
            " поставленные после них, тоже пропадут (их можно будет поставить заново). Продолжить?", yes):
        return False
    restore_backup(game, state)
    shutil.rmtree(backup_dir(game))
    say("Removed: the game folder is as it was before Void Gate.",
        "Удалено: папка игры такая же, как до установки Врат Бездны.")
    return True


def main():
    global LANG
    for a in sys.argv[1:]:
        if a in ("--lang=en", "--lang=ru"):
            LANG = a[-2:]
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    yes = "--yes" in sys.argv
    action = args.pop() if args and args[-1] in ("install", "uninstall") else None
    try:
        ctypes.windll.kernel32.SetConsoleOutputCP(65001)
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:  # noqa: BLE001
        pass
    if "uninstall" in os.path.basename(sys.argv[0]).lower():  # VoidGate_Uninstall.exe in the game folder
        say(f"{APP} {VERSION}: uninstall", f"Удаление «Врат Бездны» {VERSION}")
        game = os.path.dirname(os.path.abspath(sys.argv[0]))
        if not is_game(game) or load_state(game) is None:
            say("Void Gate is not installed here.", "Врата Бездны здесь не установлены.")
            input("\nEnter - exit / выход")
            sys.exit(1)
        say(f"\nGame: {game}", f"Игра: {game}")
        need_rights(game, [], yes)
        if not ask("\nRemove Void Gate and put the game folder back as it was before it?",
                   "Удалить Врата Бездны и вернуть папку игры как было до них?", yes):
            sys.exit(1)
        finish(uninstall(game, yes), yes)
    say(f"{APP} {VERSION} setup", f"Установка «Врат Бездны» {VERSION}")
    game = find_game(args[0] if args else None)
    while not game:
        p = input("\nDishonored 2 folder (with Dishonored2.exe) / Папка игры: ").strip().strip('"')
        game = os.path.abspath(p) if is_game(p) else None
        if not game:
            say("That is not a Dishonored 2 folder.", "Это не папка Dishonored 2.")
    say(f"\nGame: {game}", f"Игра: {game}")
    need_rights(game, [game] + ([action] if action else []), yes)
    ok, bad, n = check_build(game)
    if ok:
        say("Game build: the one the mod was made and tested on (all code checks match).",
            "Сборка игры: та, на которой мод сделан и проверен.")
    else:
        say(f"\nGame build: DIFFERENT from the one the mod was made for ({bad} of {n} code checks differ; the mod was"
            " made and tested on GOG 1.77.9.0). You can still install it, but at every start the mod will check the"
            " game and turn itself off - the game runs normally, just without Void Gate.",
            "Сборка игры: ОТЛИЧАЕТСЯ от той, для которой сделан мод (проверено только на GOG 1.77.9.0). Установить"
            " можно, но при каждом запуске мод проверит игру и выключится сам: игра будет работать как обычно, просто"
            " без Врат Бездны.")
    if action is None:
        installed = load_state(game) is not None
        say("\n  1 - " + ("Reinstall" if installed else "Install") + " / " +
            ("Переустановить" if installed else "Установить"))
        say("  2 - Uninstall / Удалить")
        say("  0 - Exit / Выход")
        action = {"1": "install", "2": "uninstall"}.get(input("> ").strip())
    done = True
    if action == "install":
        done = install(game, yes)
    elif action == "uninstall":
        done = uninstall(game, yes)
    finish(done, yes)


def finish(done, yes):
    if not yes:
        input("\nEnter - exit / выход")
    if SELF_DELETE:  # the uninstaller in the game folder removes itself once this process (and its loader) exits
        subprocess.Popen(f'cmd /c ping -n 4 127.0.0.1 >nul & del /f /q "{SELF_DELETE}"', shell=False,
                         creationflags=0x08000000 | 0x00000008)
    sys.exit(0 if done else 1)


if __name__ == "__main__":
    main()
