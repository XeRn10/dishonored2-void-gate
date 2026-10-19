"""Read-only live inspection of a running Dishonored2.exe (not elevated, so plain ReadProcessMemory works).

  py d2live.py powers        player power system: decl m_powers ids, (item -> power decl) pairs, inventory powers
  py d2live.py find <hexVA>  heap addresses holding this qword (e.g. a vtable)
"""
import ctypes
import ctypes.wintypes as wt
import struct
import subprocess
import sys

import numpy as np

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

k32 = ctypes.WinDLL("kernel32", use_last_error=True)


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_uint64), ("AllocationBase", ctypes.c_uint64), ("AllocationProtect", wt.DWORD),
                ("__a", wt.DWORD), ("RegionSize", ctypes.c_uint64), ("State", wt.DWORD), ("Protect", wt.DWORD),
                ("Type", wt.DWORD), ("__b", wt.DWORD)]


def find_pid(name="Dishonored2.exe"):
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == name.lower():
            return int(parts[1])
    sys.exit(f"{name} is not running")


class Proc:
    def __init__(self, pid):
        self.h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
        if not self.h:
            raise OSError(ctypes.get_last_error())

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_uint64(addr), buf, n, ctypes.byref(got)):
            return None
        return buf.raw[:got.value]

    def q(self, a):
        b = self.read(a, 8)
        return struct.unpack("<Q", b)[0] if b and len(b) == 8 else None

    def u32(self, a):
        b = self.read(a, 4)
        return struct.unpack("<I", b)[0] if b and len(b) == 4 else None

    def cstr(self, a, n=160):
        b = self.read(a, n)
        if not b:
            return None
        s = b.split(b"\0", 1)[0]
        return s.decode("latin1") if s and all(32 <= c < 127 for c in s) else None

    def regions(self):
        a = 0
        m = MBI()
        while k32.VirtualQueryEx(self.h, ctypes.c_uint64(a), ctypes.byref(m), ctypes.sizeof(m)):
            if m.State == MEM_COMMIT and not (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) and m.Protect & 0xEE:
                yield m.BaseAddress, m.RegionSize
            a = m.BaseAddress + m.RegionSize
            if a >= 0x7FFFFFFF0000:
                break

    def find_qword(self, value, limit=50):
        hits = []
        for base, size in self.regions():
            if 0x140000000 <= base < 0x145000000:      # skip the exe image itself
                continue
            off = 0
            while off < size:
                n = min(size - off, 64 << 20)
                b = self.read(base + off, n)
                if b:
                    arr = np.frombuffer(b[: len(b) // 8 * 8], dtype="<u8")
                    for i in np.nonzero(arr == value)[0]:
                        hits.append(base + off + int(i) * 8)
                        if len(hits) >= limit:
                            return hits
                off += n
        return hits

    def strings_in(self, obj, span=0x80):
        """Printable strings pointed to by qwords inside an object (to name a decl)."""
        out = []
        for i in range(0, span, 8):
            p = self.q(obj + i)
            if p and 0x10000 < p < 0x7FFFFFFF0000:
                s = self.cstr(p)
                if s and len(s) > 3:
                    out.append((i, s))
        return out


VT_PLAYER_POWER_SYSTEM = 0x141E88B58


def decl_name(p, decl):
    for off, s in p.strings_in(decl, 0x60):
        if "/" in s:
            return s
    return "?"


def cmd_powers(p):
    comps = p.find_qword(VT_PLAYER_POWER_SYSTEM, limit=10)
    print("arkCpntPlayerPowerSystem candidates:", [hex(c) for c in comps])
    for c in comps:
        decl = p.q(c + 8)
        if not decl:
            continue
        lst, cnt = p.q(decl + 0x68), (p.u32(decl + 0x70) or 0) & 0xFFFFFF
        ids = [p.u32(lst + 0x18 * i) for i in range(cnt)] if lst and cnt < 64 else []
        print(f"\n== component {c:#x} decl {decl:#x} ({decl_name(p, decl)})")
        print(f"   decl m_powers: {cnt} ids {ids}")
        pl, pc = p.q(c + 0x80), (p.u32(c + 0x88) or 0) & 0xFFFFFF
        print(f"   (item -> power decl) pairs: {pc}")
        for i in range(min(pc, 64)):
            item, pdecl = p.q(pl + 16 * i), p.q(pl + 16 * i + 8)
            pid = p.u32(pdecl + 0x190) if pdecl else None
            print(f"     item {item:#x} {decl_name(p, item)[:70]:70}  powerId {pid}  pdecl {pdecl and decl_name(p, pdecl)}")


def cmd_pos(p):
    """Player feet origin, render-view eye/forward, and the eye-height float setviewpos uses."""
    g = p.q(0x142710D68)
    pl = p.q(g + 0x312FF8)
    f3 = lambda a: struct.unpack("<3f", p.read(a, 12))
    print(f"player {pl:#x}  origin {f3(pl + 0x300)}")
    n = (p.u32(g + 0x1F52C0) or 0) & 0xFFFFFF
    lst = p.q(g + 0x1F52B8)
    if n and lst:
        v = p.q(lst + (n - 1) * 8) - 0x20
        print(f"view eye {f3(v + 0xD8)}  fwd {f3(v + 0xFC)}")
    print("eyeH cvar", struct.unpack("<f", p.read(0x143BFBD1C, 4))[0])


if __name__ == "__main__":
    pr = Proc(find_pid())
    if sys.argv[1] == "pos":
        cmd_pos(pr)
    elif sys.argv[1] == "powers":
        cmd_powers(pr)
    elif sys.argv[1] == "find":
        print([hex(x) for x in pr.find_qword(int(sys.argv[2], 16))])
