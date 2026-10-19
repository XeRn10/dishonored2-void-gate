"""Where are Dishonored 2's threads right now? Suspends each thread for a moment, reads RIP/RSP and a few return
addresses from its stack, resumes it. For hangs: run it two or three times and look for threads that stay put.

  py d2threads.py
"""
import ctypes
import ctypes.wintypes as wt
import struct
import sys

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from d2live import find_pid  # noqa: E402

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
TH32CS_SNAPTHREAD, TH32CS_SNAPMODULE, TH32CS_SNAPMODULE32 = 0x4, 0x8, 0x10
THREAD_ALL = 0x1F03FF
PROCESS_VM_READ, PROCESS_QUERY = 0x10, 0x400


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", ctypes.c_long), ("tpDeltaPri", ctypes.c_long),
                ("dwFlags", wt.DWORD)]


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("th32ModuleID", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("GlblcntUsage", wt.DWORD), ("ProccntUsage", wt.DWORD), ("modBaseAddr", ctypes.c_void_p),
                ("modBaseSize", wt.DWORD), ("hModule", ctypes.c_void_p), ("szModule", ctypes.c_char * 256),
                ("szExePath", ctypes.c_char * 260)]


class CONTEXT(ctypes.Structure):  # x64 CONTEXT, 16-byte aligned
    _pack_ = 16
    _fields_ = [("P", ctypes.c_uint64 * 6), ("ContextFlags", wt.DWORD), ("MxCsr", wt.DWORD),
                ("Seg", ctypes.c_uint16 * 6), ("EFlags", wt.DWORD), ("Dr", ctypes.c_uint64 * 6),
                ("Rax", ctypes.c_uint64), ("Rcx", ctypes.c_uint64), ("Rdx", ctypes.c_uint64),
                ("Rbx", ctypes.c_uint64), ("Rsp", ctypes.c_uint64), ("Rbp", ctypes.c_uint64),
                ("Rsi", ctypes.c_uint64), ("Rdi", ctypes.c_uint64), ("R", ctypes.c_uint64 * 8),
                ("Rip", ctypes.c_uint64), ("rest", ctypes.c_byte * 976)]


def main():
    pid = find_pid()
    hp = k32.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY, False, pid)
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    me = MODULEENTRY32()
    me.dwSize = ctypes.sizeof(me)
    mods = []
    ok = k32.Module32First(snap, ctypes.byref(me))
    while ok:
        mods.append((me.modBaseAddr, me.modBaseAddr + me.modBaseSize, me.szModule.decode()))
        ok = k32.Module32Next(snap, ctypes.byref(me))
    k32.CloseHandle(snap)

    def sym(a):
        for b, e, n in mods:
            if b <= a < e:
                return f"{n}+{a - b:#x}"
        return f"{a:#x}"

    def read(a, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        ok = k32.ReadProcessMemory(hp, ctypes.c_void_p(a), buf, n, ctypes.byref(got))
        return buf.raw[:got.value] if ok else b""

    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            ht = k32.OpenThread(THREAD_ALL, False, te.th32ThreadID)
            if ht:
                k32.SuspendThread(ht)
                ctx = CONTEXT()
                ctx.ContextFlags = 0x100003  # CONTROL | INTEGER
                if k32.GetThreadContext(ht, ctypes.byref(ctx)):
                    stack = read(ctx.Rsp, 0x2000)
                    rets = []
                    for i in range(0, len(stack) - 8, 8):
                        v = struct.unpack_from("<Q", stack, i)[0]
                        s = sym(v)
                        if "+" in s and not s.startswith(("ntdll", "KERNELBASE")) and len(rets) < 8:
                            pre = read(v - 5, 5)
                            if pre and pre[0] == 0xE8 or read(v - 6, 2)[:1] == b"\xff":
                                rets.append(s)
                    print(f"thread {te.th32ThreadID:6}  rip {sym(ctx.Rip):40}  stack: {' <- '.join(rets)}")
                k32.ResumeThread(ht)
                k32.CloseHandle(ht)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)


if __name__ == "__main__":
    main()
