"""Dishonored 2 datamining: cut / unused content.

  py tools\\d2datamine.py cache            read every stock decl once (all catalogs) -> scratch\\decls.pickle
  py tools\\d2datamine.py enums            engine enum values (exe strings) that no decl uses
  py tools\\d2datamine.py grep <regex>     decls whose text matches (path + first matching line)
  py tools\\d2datamine.py orphans <type>   decls of a type no other decl (or map) names
"""
import collections
import os
import pickle
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2mod import Stock, CATALOGS  # noqa: E402
from x64refs import PE  # noqa: E402

CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scratch", "decls.pickle")
SKIP = {"localized"}  # (18k subtitle/speech decls)


def load():
    if os.path.exists(CACHE):
        with open(CACHE, "rb") as f:
            return pickle.load(f)
    st = Stock()
    out, seen = {}, set()
    for g in CATALOGS:
        for p in st.index(g):
            if p in seen or not p.startswith("generated/decls/") or p.split("/")[2] in SKIP:
                continue
            seen.add(p)
            try:
                out[p] = st.read(g, p).decode("utf-8", "replace").replace("\r\n", "\n")
            except Exception as e:  # noqa: BLE001
                print("unreadable", p, e)
    with open(CACHE, "wb") as f:
        pickle.dump(out, f)
    return out


def cmd_cache():
    d = load()
    print(len(d), "decls,", sum(map(len, d.values())) // 1000000, "MB")


ENUMS = ["ARK_PLAYER_POWER_", "ARK_POWERUPGRADE_", "ARK_BONECHARM_", "ARK_ATTRIBUTE_", "ARK_HUD_ELEMENT_ITEM_",
         "ARK_GADGET_", "ARK_WEAPON_", "ARK_AMMO_", "ARK_PLAYER_STATE_", "PLAYER_STATE_", "ARK_ITEM_TYPE_",
         "DAMAGETYPE_", "ARK_TRAIT_", "ARK_UPGRADE_"]


def cmd_enums():
    d = load()
    blob = "\n".join(d.values())
    pe = PE()
    for pre in ENUMS:
        vals = sorted({s for _, s in pe.strings("^" + pre + r"[A-Z0-9_]+$")})
        if not vals:
            continue
        unused = [v for v in vals if v not in blob]
        print(f"== {pre}*  {len(vals)} values, {len(unused)} used by no decl")
        for v in unused:
            print("   ", v)


def cmd_grep(rx):
    d = load()
    r = re.compile(rx)
    n = 0
    for p, t in sorted(d.items()):
        m = r.search(t) or r.search(p)
        if m:
            line = t[t.rfind("\n", 0, m.start()) + 1:t.find("\n", m.end())].strip() if m.re.search(t) else ""
            print(p[len("generated/decls/"):], "|", line[:120])
            n += 1
    print(n, "matches")


def decl_name(p):
    # generated/decls/<type>/<name>.decl -> <name> (how decls name each other)
    return p.split("/", 3)[3][:-len(".decl")]


def cmd_orphans(typ):
    d = load()
    blob = "\n".join(d.values())
    st = Stock()
    # maps place entity defs: entity files of all catalogs (text)
    maps = []
    for g in CATALOGS:
        for p in st.index(g):
            if p.endswith(".entities"):
                try:
                    maps.append(st.read(g, p).decode("utf-8", "replace"))
                except Exception:  # noqa: BLE001
                    pass
    blob += "\n".join(maps)
    print(f"({len(maps)} map entity files searched too)")
    for p in sorted(d):
        if p.split("/")[2] != typ:
            continue
        name = decl_name(p)
        stem = name.rsplit(".", 1)[0] if "." in name.rsplit("/", 1)[-1] else name
        if blob.count(stem) <= 1:  # only its own path / self mention
            print("  ", name)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    c = sys.argv[1] if len(sys.argv) > 1 else ""
    {"cache": lambda: cmd_cache(), "enums": lambda: cmd_enums(),
     "grep": lambda: cmd_grep(sys.argv[2]), "orphans": lambda: cmd_orphans(sys.argv[2])}.get(
        c, lambda: sys.exit(__doc__))()
