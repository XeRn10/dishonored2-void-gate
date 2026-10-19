"""Dishonored 2: resolve the resource dependency tree of a decl (FX, particle, material ...) in the stock indexes.

Usage:
  py d2deps.py tree <resource name or path> [...]   print the tree with catalogs and sizes
  py d2deps.py copy <mod dir> <name> [...]          copy every dependency that is NOT in catalog 1 (game1, visible on
                                                    every map) into <mod>/files and record its index entry type/name in
                                                    <mod>/mod.json "entries", so d2install adds it to game1.

A dependency is any quoted string in a decl's text that names another resource (by its index name, e.g.
"models/effects/foo.particledt" or "models/effects/meshes/torus_1m.lwo", or a texture path).
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d2install as D  # noqa: E402

TEXT_TYPES_HINT = re.compile(rb"^\s*\{")


class Deps:
    def __init__(self):
        self.st = D.Stock()
        self.by_name = {}
        for g in D.CATALOGS:
            for i, e in enumerate(self.st.idx[g]):
                self.by_name.setdefault(e["name"].lower(), []).append((g, i))
                self.by_name.setdefault(e["path"].lower(), []).append((g, i))

    def entries(self, name):
        return [(g, self.st.idx[g][i]) for g, i in self.by_name.get(name.lower(), [])]

    def lookup(self, ref):
        """All index entries a quoted reference may mean (decls are named without extension prefix dirs)."""
        found = self.entries(ref)
        if not found and "." not in ref.rsplit("/", 1)[-1]:
            for ext in (".png", ".tga"):
                found = self.entries(ref + ext)
                if found:
                    break
        return found

    def read(self, g, e):
        return D.stock_read(self.st, e["path"])

    def walk(self, roots):
        """-> {path: (type, name, catalogs, size)} for roots and everything they reference."""
        out, todo = {}, list(roots)
        while todo:
            ref = todo.pop()
            for g, e in self.lookup(ref):
                p = e["path"]
                if p in out:
                    if g not in out[p][2]:
                        out[p][2].append(g)
                    continue
                out[p] = (e["type"], e["name"], [g], e["size"])
                data = self.read(g, e)
                if data and TEXT_TYPES_HINT.match(data[:64]):
                    # quoted decl references and bare paths (materials name their textures without quotes)
                    for m in re.finditer(rb'"([^"\n]{3,200})"|([A-Za-z0-9_.\-]+(?:/[A-Za-z0-9_.\-]+)+)', data):
                        s = (m.group(1) or m.group(2)).decode("latin1")
                        if "/" in s and s.lower() not in (x.lower() for x in roots):
                            todo.append(s)
        # catalogs for entries seen once only through one catalog: fill in from the index
        for p, (t, n, cats, size) in out.items():
            for g in D.CATALOGS:
                if p in self.st.by_path[g] and g not in cats:
                    cats.append(g)
        return out


def cmd_tree(*names):
    d = Deps()
    res = d.walk(list(names))
    total = 0
    for p, (t, n, cats, size) in sorted(res.items()):
        mark = "" if 1 in cats else "  <- not in game1"
        if 1 not in cats:
            total += size
        print(f"{t:22} {n:90} game{sorted(cats)} {size:8}{mark}")
    print(f"{len(res)} resources, {total} bytes to copy into game1")


def cmd_copy(mod, *names):
    d = Deps()
    res = d.walk(list(names))
    meta_path = os.path.join(mod, "mod.json")
    meta = json.load(open(meta_path, encoding="utf-8"))
    entries = meta.setdefault("entries", {})
    n = 0
    for p, (t, name, cats, size) in sorted(res.items()):
        if 1 in cats:
            continue
        g = sorted(cats)[0]
        e = d.st.idx[g][d.st.by_path[g][p][0]]
        dst = os.path.join(mod, "files", p)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(d.read(g, e))
        entries[p] = {"copy": True, "type": t, "name": name, "tail": bytes(e["tail"]).hex()}
        n += 1
        print(f"copied {p} (game{g}, {t})")
    with open(meta_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"{n} resources copied into {mod}")


if __name__ == "__main__":
    {"tree": cmd_tree, "copy": cmd_copy}[sys.argv[1]](*sys.argv[2:])
