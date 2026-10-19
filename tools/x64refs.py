"""Dishonored 2 (x64 PE, no ASLR, base 0x140000000): quick string / xref lookups without Ghidra.

Usage:
  py x64refs.py str <regex>              strings matching regex (VA, text)
  py x64refs.py refs <hexVA>             code refs (RIP-relative lea/mov/call/jmp) and data refs (abs qword)
  py x64refs.py sref <regex>             refs to every string matching regex
  py x64refs.py dump <hexVA> [n]         hex + qwords around VA (n bytes, default 0x80)
  py x64refs.py dis <hexVA> [n]          disassemble n instructions (capstone)
"""
import bisect
import os
import re
import struct
import sys

EXE = os.environ.get("D2EXE", r"D:/RE/targets/d2/Dishonored2.exe")


class PE:
    def __init__(self, path=EXE):
        self.d = d = open(path, "rb").read()
        pe = struct.unpack_from("<I", d, 0x3C)[0]
        nsec = struct.unpack_from("<H", d, pe + 6)[0]
        optsz = struct.unpack_from("<H", d, pe + 20)[0]
        opt = pe + 24
        self.base = struct.unpack_from("<Q", d, opt + 24)[0]
        self.secs = []
        for i in range(nsec):
            o = opt + optsz + i * 40
            name = d[o:o + 8].rstrip(b"\0").decode()
            vsz, va, rsz, ra = struct.unpack_from("<IIII", d, o + 8)
            self.secs.append((name, va, vsz, ra, rsz))
        self._crefs = None
        self._drefs = None

    def sec(self, name):
        return next(s for s in self.secs if s[0] == name)

    def off2va(self, off):
        for n, va, vsz, ra, rsz in self.secs:
            if ra <= off < ra + rsz:
                return self.base + va + off - ra
        return None

    def va2off(self, v):
        rva = v - self.base
        for n, va, vsz, ra, rsz in self.secs:
            if va <= rva < va + rsz:
                return ra + rva - va
        return None

    def read(self, v, n):
        o = self.va2off(v)
        return self.d[o:o + n]

    def cstr(self, v, maxlen=256):
        b = self.read(v, maxlen)
        return b.split(b"\0", 1)[0].decode("latin1")

    def strings(self, regex, minlen=4):
        r = re.compile(regex.encode() if isinstance(regex, str) else regex)
        for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minlen, self.d):
            if r.search(m.group()):
                yield self.off2va(m.start()), m.group().decode("latin1")

    def code_refs_index(self):
        """Map target VA -> [instruction VA] for RIP-relative operands in .text (lea/mov/cmp/call/jmp)."""
        if self._crefs is not None:
            return self._crefs
        n, va, vsz, ra, rsz = self.sec(".text")
        t = self.d[ra:ra + rsz]
        idx = {}
        # REX.W? + opcode + modrm(mod=00, rm=101) + disp32
        pat = re.compile(rb"[\x40-\x4f]?(?:\x8d|\x8b|\x89|\x3b|\x39|\x80|\x81|\x83|\xc7|\x0f[\x10\x11\x28\x29\xb6\xb7\xbe\xbf])[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", re.S)
        for m in pat.finditer(t):
            end = m.end()
            if end + 4 > len(t):
                continue
            disp = struct.unpack_from("<i", t, end)[0]
            ins_end = end + 4
            # instructions with an imm after disp (c7 /0 imm32, 80/83 imm8, 81 imm32)
            op = t[m.end() - 2:m.end() - 1]
            if op == b"\xc7" or op == b"\x81":
                ins_end += 4
            elif op in (b"\x80", b"\x83"):
                ins_end += 1
            tgt = self.base + va + ins_end + disp
            idx.setdefault(tgt, []).append(self.base + va + m.start())
        for m in re.finditer(rb"[\xe8\xe9]", t):
            pass  # calls are handled by refs() on demand (too many to index cheaply)
        self._crefs = idx
        return idx

    def data_refs_index(self):
        if self._drefs is not None:
            return self._drefs
        idx = {}
        lo, hi = self.base, self.base + max(va + vsz for _, va, vsz, _, _ in self.secs)
        for name in (".rdata", ".data", "_RDATA", ".mydata"):
            try:
                n, va, vsz, ra, rsz = self.sec(name)
            except StopIteration:
                continue
            blk = self.d[ra:ra + rsz]
            for i in range(0, len(blk) - 7, 8):
                q = struct.unpack_from("<Q", blk, i)[0]
                if lo <= q < hi:
                    idx.setdefault(q, []).append(self.base + va + i)
        self._drefs = idx
        return idx

    def call_refs(self, target):
        n, va, vsz, ra, rsz = self.sec(".text")
        t = self.d[ra:ra + rsz]
        out = []
        for m in re.finditer(rb"[\xe8\xe9]", t):
            i = m.start()
            if i + 5 > len(t):
                continue
            rel = struct.unpack_from("<i", t, i + 1)[0]
            if self.base + va + i + 5 + rel == target:
                out.append(self.base + va + i)
        return out

    def refs(self, target, calls=False):
        c = self.code_refs_index().get(target, [])
        d = self.data_refs_index().get(target, [])
        k = self.call_refs(target) if calls else []
        return c, d, k

    def dis(self, v, count=30):
        from capstone import Cs, CS_ARCH_X86, CS_MODE_64
        md = Cs(CS_ARCH_X86, CS_MODE_64)
        out = []
        for ins in md.disasm(self.read(v, count * 15), v):
            out.append(f"{ins.address:x}  {ins.mnemonic:8} {ins.op_str}")
            if len(out) >= count:
                break
        return out

    def func_start(self, v):
        """Function containing VA, via .pdata RUNTIME_FUNCTION table."""
        n, va, vsz, ra, rsz = self.sec(".pdata")
        if not hasattr(self, "_pdata"):
            self._pdata = []
            for i in range(0, vsz - 11, 12):
                b, e, u = struct.unpack_from("<III", self.d, ra + i)
                if b == 0:
                    break
                self._pdata.append((b, e))
            self._pdata.sort()
            self._pstarts = [b for b, _ in self._pdata]
        rva = v - self.base
        i = bisect.bisect_right(self._pstarts, rva) - 1
        if i >= 0 and self._pdata[i][0] <= rva < self._pdata[i][1]:
            return self.base + self._pdata[i][0], self.base + self._pdata[i][1]
        return None


def main():
    p = PE()
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd == "str":
        for v, s in p.strings(args[0]):
            print(f"{v:x}  {s}")
    elif cmd in ("refs", "sref"):
        targets = [(int(args[0], 16), "")] if cmd == "refs" else list(p.strings(args[0]))
        for v, s in targets:
            c, d, k = p.refs(v, calls=(cmd == "refs"))
            print(f"== {v:x} {s}")
            for x in c:
                f = p.func_start(x)
                print(f"   code {x:x}" + (f"  in func {f[0]:x}" if f else ""))
            for x in d:
                print(f"   data {x:x}")
            for x in k:
                f = p.func_start(x)
                print(f"   call {x:x}" + (f"  in func {f[0]:x}" if f else ""))
    elif cmd == "dump":
        v = int(args[0], 16)
        n = int(args[1], 16) if len(args) > 1 else 0x80
        b = p.read(v, n)
        for i in range(0, n, 16):
            row = b[i:i + 16]
            qs = " ".join(f"{struct.unpack_from('<Q', row, j)[0]:016x}" for j in range(0, len(row) - 7, 8))
            print(f"{v + i:x}  {row.hex(' ')}  | {qs}")
    elif cmd == "dis":
        v = int(args[0], 16)
        n = int(args[1]) if len(args) > 1 else 30
        print("\n".join(p.dis(v, n)))


if __name__ == "__main__":
    main()
