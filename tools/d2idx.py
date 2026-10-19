"""Dishonored 2 (Void Engine) .index/.resources reader.

Index layout (big-endian unless noted):
  0x00  "\\x05SER" magic
  0x04  u32 index size
  0x08  24 bytes reserved
  0x20  u32 entry count
  entry: u32 id, 3x (u32 LE len + latin1 str: type, name, path),
         u64 offset, u32 size, u32 zsize, 10 bytes tail
  tail[9]: bits 0-1 = game number - 1, bits 2+ = container (base, _001.._005, _patch)
  Data is zlib when zsize != size, stored otherwise.

Usage:
  python d2idx.py list <regex>            list matching paths
  python d2idx.py extract <regex> <dir>   extract matching files
"""
import os
import re
import struct
import sys
import zlib

BASE = r"D:/Dishonored 2/base"
SUFFIXES = ["", "_001", "_002", "_003", "_004", "_005", "_patch"]


def read_index(path):
    d = open(path, "rb").read()
    assert d[:4] == b"\x05SER", "bad magic"
    count, = struct.unpack_from(">I", d, 0x20)
    o = 0x24
    out = []

    def s():
        nonlocal o
        n, = struct.unpack_from("<I", d, o)
        o += 4
        r = d[o:o + n].decode("latin1")
        o += n
        return r

    for _ in range(count):
        eid, = struct.unpack_from(">I", d, o)
        o += 4
        typ = s()
        name = s()
        p = s()
        off, size, zsize = struct.unpack_from(">QII", d, o)
        o += 16
        tail = d[o:o + 10]
        o += 10
        out.append(dict(id=eid, type=typ, name=name, path=p, off=off, size=size, zsize=zsize, tail=tail))
    return out


class Archive:
    """Merged view over game1..game3 using the latest (_patch) index of each."""

    def __init__(self, base=BASE):
        self.base = base
        self.entries = {}
        self._fh = {}
        for g in (1, 2, 3):
            for x in read_index(os.path.join(base, f"game{g}_patch.index")):
                self.entries.setdefault(x["path"], x)

    def container(self, x):
        t = x["tail"][9]
        return f"game{(t & 3) + 1}{SUFFIXES[t >> 2]}.resources"

    def read(self, path_or_entry):
        x = self.entries[path_or_entry] if isinstance(path_or_entry, str) else path_or_entry
        fn = self.container(x)
        f = self._fh.get(fn)
        if f is None:
            f = self._fh[fn] = open(os.path.join(self.base, fn), "rb")
        f.seek(x["off"])
        raw = f.read(x["zsize"])
        if x["zsize"] != x["size"]:
            raw = zlib.decompress(raw)
        return raw

    def find(self, regex, types=None):
        r = re.compile(regex, re.I)
        return [x for p, x in self.entries.items() if r.search(p) and (types is None or x["type"] in types)]

    def extract(self, regex, out_dir, types=None):
        n = 0
        for x in self.find(regex, types):
            dst = os.path.join(out_dir, x["path"])
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(self.read(x))
            n += 1
        return n


if __name__ == "__main__":
    a = Archive()
    cmd = sys.argv[1]
    if cmd == "list":
        for x in sorted(a.find(sys.argv[2]), key=lambda x: x["path"]):
            print(f'{x["type"]:22} {x["size"]:9} {x["path"]}')
    elif cmd == "extract":
        print(a.extract(sys.argv[2], sys.argv[3]), "files extracted")
