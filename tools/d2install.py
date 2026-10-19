"""Dishonored 2: install our data mods directly into the game (replaces Void Installer for d2mods/*).

Builds base/gameN_voidmod.index + gameN_voidmod.resources from the STOCK gameN_patch.index (kept in
backups/d2_base_2026-10-10) plus the given mods, and writes master.index exactly like Void Installer does
(index names -> gameN_voidmod.index, data files gameN_voidmod.resources appended as container 7).

Mods use the d2mod.py layout: <mod>/mod.json, <mod>/files/<resource path>.
  * a path that exists in a stock catalog replaces it in every catalog that has it;
  * a NEW path is added to the catalogs listed in mod.json "new_catalogs" (default [1] = game1, the common
    catalog every map can see). type/name are derived from the path:
      generated/decls/<dir>/<name>.decl   -> type = type used by stock entries in <dir>, name = <name>
      generated/image/<x>.bimage7         -> type "image", name "<x>.png"
      anything else                        -> type "File", name = path
    or given explicitly in mod.json "entries": {"<path>": {"type": ..., "name": ...}}.
  Later mods override earlier ones for the same path.
  <mod>/lang/<stem>.json ({"key": "value"}) adds/overrides keys in strings/<stem>.lang on top of whatever an
  earlier mod (or stock) provides, so several mods can add text without clobbering each other.

Usage (game must be closed):
  py d2install.py status                      what the installed voidmod changes vs stock, per catalog
  py d2install.py install <mod> [<mod> ...]   back up current base/*.index + voidmod files, then install
  py d2install.py uninstall                   back up, then restore stock indexes / master.index
  py d2install.py dry <mod> [<mod> ...]       show what install would do, write nothing
"""
import datetime
import json
import os
import re
import shutil
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2idx import read_index  # noqa: E402

GAME = r"D:/Dishonored 2"
BASE = os.path.join(GAME, "base")
STOCK = r"D:/DishonoredMod/backups/d2_base_2026-10-10"
BACKUPS = r"D:/DishonoredMod/backups"
CATALOGS = (1, 2, 3, 4)
VOIDMOD_CONTAINER = 7          # 0 base, 1.._5 = _001.._005, 6 _patch, 7 _voidmod
FLAG_FILE = 0x20               # tail[7] for images / plain files in stock


def write_index(path, entries):
    body = bytearray(struct.pack(">I", len(entries)))
    for e in entries:
        body += struct.pack(">I", e["id"])
        for k in ("type", "name", "path"):
            s = e[k].encode("latin1")
            body += struct.pack("<I", len(s)) + s
        body += struct.pack(">QII", e["off"], e["size"], e["zsize"]) + bytes(e["tail"])
    # header: magic, u32 BE (file size - 32), 24 reserved zero bytes, then the body (count + entries)
    data = b"\x05SER" + struct.pack(">I", len(body) + 0x20 - 32) + bytes(24) + bytes(body)
    open(path, "wb").write(data)


def master_index():
    """master.index as Void Installer writes it (verified byte-for-byte against its output)."""
    out = bytearray(b"\x04SER" + struct.pack("<HH", 0x200, 0x400))
    for g in CATALOGS:
        for s in (f"game{g}_voidmod.index", f"game{g}.resources"):
            out += struct.pack("<I", len(s)) + s.encode()
    extras = [(f"game{g}{suf}.resources", g - 1) for suf in ("_001", "_002", "_003", "_004", "_005", "_patch", "_voidmod")
              for g in CATALOGS]
    out += struct.pack(">I", len(extras))
    for name, c in extras:
        out += struct.pack("<I", len(name)) + name.encode() + struct.pack(">H", c)
    stock = open(os.path.join(STOCK, "master.index"), "rb").read()
    tail = stock[stock.rindex(b"\x00\x00\x00\x01\x00\x00\x00\x02"):]
    return bytes(out + tail)


def compress(data):
    c = zlib.compressobj(1, zlib.DEFLATED, 10)
    z = c.compress(data) + c.flush()
    return z if len(z) < len(data) else data


class Stock:
    def __init__(self):
        self.idx = {g: read_index(os.path.join(STOCK, f"game{g}_patch.index")) for g in CATALOGS}
        self.by_path = {g: {} for g in CATALOGS}
        for g in CATALOGS:
            for i, e in enumerate(self.idx[g]):
                self.by_path[g].setdefault(e["path"], []).append(i)
        self.dir_type = {}
        for g in CATALOGS:
            for e in self.idx[g]:
                m = re.match(r"generated/decls/([^/]+)/", e["path"])
                if m:
                    self.dir_type.setdefault(m.group(1), e["type"])

    def derive(self, path):
        m = re.match(r"generated/decls/([^/]+)/(.+)\.decl$", path)
        if m:
            if m.group(1) not in self.dir_type:
                sys.exit(f"{path}: unknown decl dir '{m.group(1)}', give type/name in mod.json entries")
            return self.dir_type[m.group(1)], m.group(2), 0
        m = re.match(r"generated/image/(.+)\.bimage7$", path)
        if m:
            return "image", m.group(1) + ".png", FLAG_FILE
        return "File", path, FLAG_FILE


def mod_files(mod):
    root = os.path.join(mod, "files")
    for dp, _, fns in os.walk(root):
        for fn in fns:
            full = os.path.join(dp, fn)
            yield os.path.relpath(full, root).replace(os.sep, "/"), full


def stock_read(st, path):
    for g in CATALOGS:
        if path in st.by_path[g]:
            e = st.idx[g][st.by_path[g][path][0]]
            fn = f"game{g}{['', '_001', '_002', '_003', '_004', '_005', '_patch'][e['tail'][9] >> 2]}.resources"
            with open(os.path.join(BASE, fn), "rb") as f:
                f.seek(e["off"])
                raw = f.read(e["zsize"])
            return zlib.decompress(raw) if e["zsize"] != e["size"] else raw
    raise KeyError(path)


def merge_lang(data, entries):
    """Set "key" -> "value" lines in a .lang file (tab-indented `"key"<TAB>"value"` lines inside { })."""
    text = data.decode("utf-8")
    lines = text.split("\n")
    pos = {}
    for i, ln in enumerate(lines):
        m = re.match(r'\t"([^"]+)"\t', ln)
        if m:
            pos[m.group(1)] = i
    close = max(i for i, ln in enumerate(lines) if ln.strip() == "}")
    added = []
    for k, v in entries.items():
        if '"' in v or '"' in k:
            sys.exit(f"lang key {k}: double quotes are not allowed, use typographic quotes")
        ln = f'\t"{k}"\t"{v}"'
        if k in pos:
            lines[pos[k]] = ln
        else:
            added.append(ln)
    lines[close:close] = added
    return "\n".join(lines).encode("utf-8")


def plan(mods):
    st = Stock()
    files = {}          # path -> (data, mod, info)
    for mod in mods:
        info = json.load(open(os.path.join(mod, "mod.json"), encoding="utf-8"))
        modname = os.path.basename(os.path.normpath(mod))
        for path, full in mod_files(mod):
            files[path] = (open(full, "rb").read(), modname, info)
        for path, ent in info.get("entries", {}).items():  # stock copies not kept in the mod: from the game
            if ent.get("copy") and path not in files:
                files[path] = (stock_read(st, path), modname, info)
        langdir = os.path.join(mod, "lang")
        if os.path.isdir(langdir):
            for fn in sorted(os.listdir(langdir)):
                if not fn.endswith(".json"):
                    continue
                path = f"strings/{fn[:-5]}.lang"
                base = files[path][0] if path in files else stock_read(st, path)
                entries = json.load(open(os.path.join(langdir, fn), encoding="utf-8"))
                files[path] = (merge_lang(base, entries), modname, info)
    per = {g: {"replace": {}, "new": []} for g in CATALOGS}
    for path, (data, modname, info) in sorted(files.items()):
        gs = [g for g in CATALOGS if path in st.by_path[g]]
        ent = info.get("entries", {}).get(path, {})
        if ent.get("copy"):
            # a stock resource of another catalog (d2deps.py copy): add it where it is missing, keep the stock
            # copies untouched
            for g in info.get("new_catalogs", [1]):
                if g not in gs:
                    per[g]["new"].append(dict(path=path, type=ent["type"], name=ent["name"], flag=0,
                                              tail=bytes.fromhex(ent["tail"]), data=data))
            continue
        if gs:
            for g in gs:
                per[g]["replace"][path] = data
            continue
        ent = info.get("entries", {}).get(path, {})
        typ, name, flag = st.derive(path)
        typ, name = ent.get("type", typ), ent.get("name", name)
        extra = {"tail": bytes.fromhex(ent["tail"])} if "tail" in ent else {}  # (e.g. a stock model's, for a variant)
        for g in info.get("new_catalogs", [1]):
            per[g]["new"].append(dict(path=path, type=typ, name=name, flag=flag, data=data, **extra))
    return st, per


def order_violations(entries):
    """Indexes i where entry i-1 and i live in the same data file but the offset goes backwards."""
    return {i for i in range(1, len(entries))
            if entries[i - 1]["tail"][9] == entries[i]["tail"][9] and entries[i - 1]["off"] > entries[i]["off"]}


def build(st, per, out_dir):
    report = []
    for g in CATALOGS:
        entries = [dict(e, tail=bytearray(e["tail"])) for e in st.idx[g]]
        blob = bytearray(b"\x04SER")
        tail_container = (VOIDMOD_CONTAINER << 2) | (g - 1)
        # The engine's background loader requires that consecutive index entries in the same data file have
        # non-decreasing offsets ("idBackgroundLoader::BeginBackgroundLoads: FileTable is out of order"), so the
        # voidmod blob is written strictly in index order, one copy per entry, and new entries go last.
        data_for = {}
        for path, data in sorted(per[g]["replace"].items()):
            for i in st.by_path[g][path]:
                data_for[i] = data
            report.append(f"game{g}  replace {len(data):8}  {path}")
        for i in sorted(data_for):
            data = data_for[i]
            z = compress(data)
            e = entries[i]
            e["off"], e["size"], e["zsize"] = len(blob), len(data), len(z)
            e["tail"][9] = tail_container
            blob += z
        next_id = max((e["id"] for e in entries), default=-1) + 1
        for n in per[g]["new"]:
            z = compress(n["data"])
            tail = bytearray(n["tail"]) if "tail" in n else bytearray(10)
            if "tail" not in n:
                tail[7] = n["flag"]
            tail[9] = tail_container
            entries.append(dict(id=next_id, type=n["type"], name=n["name"], path=n["path"], off=len(blob),
                                size=len(n["data"]), zsize=len(z), tail=tail))
            report.append(f"game{g}  NEW id={next_id} {n['type']} '{n['name']}'  {len(n['data'])} b  {n['path']}")
            next_id += 1
            blob += z
        new_bad = order_violations(entries) - order_violations(st.idx[g])
        if new_bad:
            sys.exit(f"game{g}: index order would break the background loader at entries {sorted(new_bad)[:10]}")
        if out_dir:
            write_index(os.path.join(out_dir, f"game{g}_voidmod.index"), entries)
            open(os.path.join(out_dir, f"game{g}_voidmod.resources"), "wb").write(blob)
    if out_dir:
        open(os.path.join(out_dir, "master.index"), "wb").write(master_index())
    return report


def backup_current():
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    dst = os.path.join(BACKUPS, f"d2_installed_{stamp}")
    os.makedirs(dst)
    for fn in os.listdir(BASE):
        if fn == "master.index" or fn.endswith("_voidmod.index") or fn.endswith("_voidmod.resources"):
            shutil.copy2(os.path.join(BASE, fn), dst)
    print("backup:", dst)
    return dst


def game_running():
    import subprocess
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq Dishonored2.exe"], capture_output=True, text=True).stdout
    return "Dishonored2.exe" in out


def cmd_status():
    st = Stock()
    for g in CATALOGS:
        cur_path = os.path.join(BASE, f"game{g}_voidmod.index")
        if not os.path.exists(cur_path):
            print(f"game{g}: no voidmod index")
            continue
        cur = read_index(cur_path)
        changed = [c["path"] for c, s in zip(cur, st.idx[g]) if (c["off"], c["size"], c["tail"]) != (s["off"], s["size"], s["tail"])]
        added = [c["path"] for c in cur[len(st.idx[g]):]]
        print(f"game{g}: {len(changed)} replaced, {len(added)} new")
        for p in changed + [f"NEW {p}" for p in added]:
            print("   ", p)


def cmd_install(*mods, dry=False):
    if not dry and game_running():
        sys.exit("Dishonored2.exe is running - close the game first")
    st, per = plan(mods)
    if dry:
        print("\n".join(build(st, per, None)))
        return
    tmp = os.path.join(BACKUPS, "_install_tmp")
    shutil.rmtree(tmp, ignore_errors=True)
    os.makedirs(tmp)
    print("\n".join(build(st, per, tmp)))
    backup_current()
    for fn in os.listdir(tmp):
        shutil.move(os.path.join(tmp, fn), os.path.join(BASE, fn))
    os.rmdir(tmp)
    print("installed:", ", ".join(os.path.basename(os.path.normpath(m)) for m in mods))


def cmd_uninstall():
    if game_running():
        sys.exit("Dishonored2.exe is running - close the game first")
    backup_current()
    shutil.copy2(os.path.join(STOCK, "master.index"), os.path.join(BASE, "master.index"))
    for fn in os.listdir(BASE):
        if fn.endswith("_voidmod.index") or fn.endswith("_voidmod.resources"):
            os.remove(os.path.join(BASE, fn))
    print("restored stock master.index, removed voidmod files")


if __name__ == "__main__":
    c, a = sys.argv[1], sys.argv[2:]
    if c == "status":
        cmd_status()
    elif c == "install":
        cmd_install(*a)
    elif c == "dry":
        cmd_install(*a, dry=True)
    elif c == "uninstall":
        cmd_uninstall()
    else:
        sys.exit(__doc__)
