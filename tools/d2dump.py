"""Dishonored 2 crash dump reader: the faulting thread's registers and a call-stack guess.

  py d2dump.py [dump]   (default: D:/Dishonored 2/dlss5-feed-crash.dmp)

The stack is found by scanning the faulting thread's stack memory for return addresses: values that point into a
loaded module right after a call instruction. Dishonored2.exe addresses are shown with their function start (.pdata)
so they can be fed to tools/gdec.cmd.
"""
import bisect
import struct
import sys

from minidump.minidumpfile import MinidumpFile

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from x64refs import PE  # noqa: E402

DUMP = sys.argv[1] if len(sys.argv) > 1 else r"D:/Dishonored 2/dlss5-feed-crash.dmp"


def main():
    md = MinidumpFile.parse(DUMP)
    reader = md.get_reader()
    mods = sorted((m.baseaddress, m.baseaddress + m.size, m.name.split("\\")[-1]) for m in md.modules.modules)
    starts = [m[0] for m in mods]

    def mod_of(a):
        i = bisect.bisect_right(starts, a) - 1
        return mods[i] if i >= 0 and a < mods[i][1] else None

    exe = PE()
    pn, pva, pvsz, pra, prsz = exe.sec(".pdata")
    fns = sorted((struct.unpack_from("<I", exe.d, o)[0] + exe.base, struct.unpack_from("<I", exe.d, o + 4)[0] + exe.base)
                 for o in range(pra, pra + pvsz, 12) if struct.unpack_from("<I", exe.d, o)[0])
    fstarts = [f[0] for f in fns]

    def sym(a):
        m = mod_of(a)
        if not m:
            return f"{a:#x}"
        s = f"{m[2]}+{a - m[0]:#x}"
        if m[2].lower() == "dishonored2.exe":
            i = bisect.bisect_right(fstarts, a) - 1
            if i >= 0 and a < fns[i][1]:
                s += f"  (func {fns[i][0]:#x})"
        return s

    exc = md.exception.exception_records[0]
    rec = exc.ExceptionRecord
    print(f"exception {rec.ExceptionCode} at {sym(rec.ExceptionAddress)} thread {exc.ThreadId}")
    print("  params", [hex(x) for x in rec.ExceptionInformation[:rec.NumberParameters]])
    th = next(t for t in md.threads.threads if t.ThreadId == exc.ThreadId)
    with open(DUMP, "rb") as f:  # the context lives at a file offset, not in the dumped memory
        f.seek(exc.ThreadContext.Rva)
        ctx_raw = f.read(exc.ThreadContext.DataSize)
    # CONTEXT (x64): Rax at 0x78 ... R15 at 0xF0, Rip at 0xF8
    names = ["rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14",
             "r15", "rip"]
    regs = dict(zip(names, struct.unpack_from("<17Q", ctx_raw, 0x78)))
    for i in range(0, len(names), 4):
        print("  " + "  ".join(f"{n}={regs[n]:016x}" for n in names[i:i + 4]))
    rsp = regs["rsp"]
    try:
        stack = reader.read(rsp, 0x4000)
    except Exception:
        stack = reader.read(rsp, 0x1000)
    print("call stack (return addresses found on the stack):")
    n = 0
    for off in range(0, len(stack) - 8, 8):
        v = struct.unpack_from("<Q", stack, off)[0]
        m = mod_of(v)
        if not m:
            continue
        try:
            before = reader.read(v - 6, 6)
        except Exception:
            continue
        # call rel32 (E8 xx xx xx xx) or call [rip+x] / call reg variants (FF /2)
        if before[1] == 0xE8 or before[0] == 0xFF and before[1] in (0x15,) or before[4] == 0xFF and (before[5] & 0x38) == 0x10 \
                or before[3] == 0xFF and (before[4] & 0x38) == 0x10 or before[2] == 0xFF and (before[3] & 0x38) == 0x10:
            print(f"  [rsp+{off:#06x}] {sym(v)}")
            n += 1
            if n >= 40:
                break


if __name__ == "__main__":
    main()
