"""Dishonored 2: build Void Installer packages from edited resources.

Mod source folder:
  <mod>/mod.json           {"name": "...", "version": "1.0", "author": "..."}
  <mod>/files/<path>       edited resource, path as in the index
                           (e.g. files/generated/decls/damage/.../windblast_slam.damage.decl)

The package replaces the resource in every catalog (game1..game4) whose stock index has it.
Data is zlib with a 1 KB window (wbits=10, level 1), like the stock archives.

Package layout (read by Void Installer):
  <out>/<name> v<version>/ModInfo.xml
  <out>/<name> v<version>/Resources/gameN.voidIndex    "VIdx", u8 3, u32 count; entry: u32 len + path,
                                                       i32 -1, u8 0, u64 off, u32 zsize, u32 size (LE)
  <out>/<name> v<version>/Resources/gameN.voidRessources  zlib blocks back to back

Usage:
  py d2mod.py extract <mod> <regex>      copy stock resources into <mod>/files (existing files are kept)
  py d2mod.py diff <mod>                 unified diff of edited text resources against stock
  py d2mod.py check <mod>                bracket/quote balance of edited decls
  py d2mod.py build <mod> <out_dir>      write the Void Installer package
"""
import difflib
import json
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2idx import BASE, SUFFIXES, read_index

CATALOGS = (1, 2, 3, 4)


class Stock:
    """Stock (unmodded) view: gameN_patch.index of every catalog, read lazily."""

    def __init__(self, base=BASE):
        self.base = base
        self._idx = {}
        self._fh = {}

    def index(self, g):
        if g not in self._idx:
            self._idx[g] = {x["path"]: x for x in read_index(os.path.join(self.base, f"game{g}_patch.index"))}
        return self._idx[g]

    def catalogs(self, path):
        return [g for g in CATALOGS if path in self.index(g)]

    def read(self, g, path):
        x = self.index(g)[path]
        fn = f"game{g}{SUFFIXES[x['tail'][9] >> 2]}.resources"
        f = self._fh.get(fn)
        if f is None:
            f = self._fh[fn] = open(os.path.join(self.base, fn), "rb")
        f.seek(x["off"])
        raw = f.read(x["zsize"])
        return zlib.decompress(raw) if x["zsize"] != x["size"] else raw


def mod_files(mod):
    root = os.path.join(mod, "files")
    for dp, _, fns in os.walk(root):
        for fn in fns:
            full = os.path.join(dp, fn)
            yield os.path.relpath(full, root).replace(os.sep, "/"), full


def compress(data):
    c = zlib.compressobj(1, zlib.DEFLATED, 10)
    z = c.compress(data) + c.flush()
    return z if len(z) < len(data) else data


def check_decl(text):
    """Return a list of problems: unbalanced {} [] (), unterminated strings or comments."""
    errs, stack, i, line = [], [], 0, 1
    pairs = {"}": "{", "]": "[", ")": "("}
    while i < len(text):
        ch = text[i]
        if ch == "\n":
            line += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = len(text) if j < 0 else j
            continue
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            if j < 0:
                errs.append(f"line {line}: unterminated /* comment")
                break
            line += text.count("\n", i, j)
            i = j + 2
            continue
        elif ch == '"':
            j = i + 1
            while j < len(text) and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            if j >= len(text):
                errs.append(f"line {line}: unterminated string")
                break
            line += text.count("\n", i, j)
            i = j + 1
            continue
        elif ch in "{[(":
            stack.append((ch, line))
        elif ch in "}])":
            if not stack or stack[-1][0] != pairs[ch]:
                errs.append(f"line {line}: unexpected '{ch}'")
            else:
                stack.pop()
        i += 1
    errs += [f"line {ln}: unclosed '{ch}'" for ch, ln in stack]
    return errs


def cmd_extract(mod, regex):
    st, r, n = Stock(), re.compile(regex, re.I), 0
    for g in CATALOGS:
        for path in st.index(g):
            if not r.search(path):
                continue
            dst = os.path.join(mod, "files", path)
            if os.path.exists(dst):
                continue
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(st.read(g, path))
            print(f"game{g}  {path}")
            n += 1
    print(n, "files extracted")


def cmd_diff(mod):
    st = Stock()
    for path, full in sorted(mod_files(mod)):
        gs = st.catalogs(path)
        if not gs:
            print(f"!! {path}: not in any stock index (new resource)")
            continue
        old = st.read(gs[0], path).decode("utf-8", "replace").splitlines()
        new = open(full, "rb").read().decode("utf-8", "replace").splitlines()
        for ln in difflib.unified_diff(old, new, f"stock/{path}", f"mod/{path}", lineterm="", n=2):
            print(ln)


def cmd_check(mod):
    bad = 0
    for path, full in sorted(mod_files(mod)):
        if not path.endswith(".decl"):
            continue
        for e in check_decl(open(full, "rb").read().decode("utf-8", "replace")):
            print(f"{path}: {e}")
            bad += 1
    print("OK" if not bad else f"{bad} problem(s)")
    return bad


def cmd_build(mod, out_dir):
    info = json.load(open(os.path.join(mod, "mod.json"), encoding="utf-8"))
    if cmd_check(mod):
        sys.exit("decl check failed")
    st = Stock()
    per = {g: [] for g in CATALOGS}
    for path, full in sorted(mod_files(mod)):
        gs = st.catalogs(path)
        if not gs:
            sys.exit(f"{path}: not in any stock index; new resources are not supported")
        stock = [st.read(g, path) for g in gs]
        if any(s != stock[0] for s in stock[1:]):
            sys.exit(f"{path}: stock copies differ between catalogs {gs}; edit them separately")
        data = open(full, "rb").read()
        if data == stock[0]:
            print(f"   unchanged, skipped: {path}")
            continue
        for g in gs:
            per[g].append((path, data))

    pkg = os.path.join(out_dir, f"{info['name']} v{info['version']}")
    res = os.path.join(pkg, "Resources")
    os.makedirs(res, exist_ok=True)
    for fn in os.listdir(res):
        os.remove(os.path.join(res, fn))
    for g, items in per.items():
        if not items:
            continue
        idx, blob = [b"VIdx", bytes([3]), struct.pack("<I", len(items))], bytearray()
        for path, data in items:
            z = compress(data)
            p = path.encode("utf-8")
            idx += [struct.pack("<I", len(p)), p, struct.pack("<iBQII", -1, 0, len(blob), len(z), len(data))]
            blob += z
            print(f"game{g}  {len(data):7} -> {len(z):6}  {path}")
        open(os.path.join(res, f"game{g}.voidIndex"), "wb").write(b"".join(idx))
        open(os.path.join(res, f"game{g}.voidRessources"), "wb").write(blob)
    xml = '<ModInfo modName="{name}" version="{version}" author="{author}" />'.format(**info)
    open(os.path.join(pkg, "ModInfo.xml"), "w", encoding="utf-8").write(xml)
    print("package:", pkg)


if __name__ == "__main__":
    cmd, args = sys.argv[1], sys.argv[2:]
    {"extract": cmd_extract, "diff": cmd_diff, "check": cmd_check, "build": cmd_build}[cmd](*args)
