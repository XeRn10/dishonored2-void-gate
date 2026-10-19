r"""Decompile / inspect Dishonored2.exe from the analyzed Ghidra project (read-only), via pyghidra.

  tools\gdec.cmd <cmd> ...          (pyghidra-mcp tool env: pyghidra + JDK)
  dec <addr> [<addr> ...]        decompile the functions containing these addresses
  xrefs <addr>                   references to an address (from -> type, containing function)
  name <addr>                    function / symbol at address
  vt <class>                     vtable(s) of an RTTI class name (e.g. arkCpntPowerGrapple) and their slots
  find <regex>                   functions / symbols whose name matches
  serve                          keep the project open and answer the commands above on 127.0.0.1:47555

Opening the project takes most of a call's time and Ghidra locks it, so only one process can have it open: with
a server running (tools\gdec.cmd serve, started once in the background), every gdec call -- plain `py gdec.py ...`
works too, no pyghidra needed -- is sent to it and comes back in a moment, and several callers can share it.
Output goes to stdout (UTF-8). Addresses are hex VAs (0x140000000-based, as in x64refs.py).
"""
import contextlib
import io
import json
import os
import re
import socket
import sys

PROJ_DIR = r"D:/RE/ghidra-projects"
PROJ_NAME = "Dishonored2"
PORT = 47555


def via_server(argv):
    """Run a command in the server; None if no server is listening."""
    try:
        s = socket.create_connection(("127.0.0.1", PORT), timeout=2)
    except OSError:
        return None
    with s:
        s.settimeout(600)
        s.sendall(json.dumps(argv).encode() + b"\n")
        chunks = []
        while True:
            b = s.recv(65536)
            if not b:
                break
            chunks.append(b)
    return b"".join(chunks).decode("utf-8", "replace")


class Ghidra:
    def __init__(self):
        import pyghidra
        pyghidra.start(install_dir=os.environ.get("GHIDRA_INSTALL_DIR", r"D:/RE/ghidra_12.1.4_PUBLIC"))
        from ghidra.app.decompiler import DecompInterface, DecompileOptions
        from ghidra.base.project import GhidraProject
        from ghidra.util.task import ConsoleTaskMonitor
        self.proj = GhidraProject.openProject(PROJ_DIR, PROJ_NAME, True)
        self.prog = prog = self.proj.openProgram("/", "Dishonored2.exe", True)
        self.af = prog.getAddressFactory()
        self.fm = prog.getFunctionManager()
        self.st = prog.getSymbolTable()
        self.rm = prog.getReferenceManager()
        self.mon = ConsoleTaskMonitor()
        self.di = DecompInterface()
        self.di.setOptions(DecompileOptions())
        self.di.openProgram(prog)

    def A(self, x):
        return self.af.getAddress(hex(int(x, 16)) if isinstance(x, str) else hex(x))

    def fname(self, addr):
        f = self.fm.getFunctionContaining(addr)
        return f"{f.getName()}@{f.getEntryPoint()}" if f else "?"

    def cmd_dec(self, *addrs):
        seen = set()
        for a in addrs:
            f = self.fm.getFunctionContaining(self.A(a))
            if f is None or f.getEntryPoint() in seen:
                print(f"// no function at {a}" if f is None else "")
                continue
            seen.add(f.getEntryPoint())
            r = self.di.decompileFunction(f, 120, self.mon)
            print(f"// ===== {f.getName()} @ {f.getEntryPoint()} (size {f.getBody().getNumAddresses()})")
            print(r.getDecompiledFunction().getC() if r.decompileCompleted() else f"// failed: {r.getErrorMessage()}")

    def cmd_xrefs(self, a):
        for r in self.rm.getReferencesTo(self.A(a)):
            print(f"{r.getFromAddress()}  {r.getReferenceType()}  in {self.fname(r.getFromAddress())}")

    def cmd_name(self, a):
        addr = self.A(a)
        print("function:", self.fname(addr))
        for s in self.st.getSymbols(addr):
            print("symbol:", s.getName(True))

    def cmd_vt(self, cls):
        mem = self.prog.getMemory()
        for s in self.st.getSymbols("vftable"):
            ns = s.getParentNamespace().getName(True)
            if re.fullmatch(cls, ns.split("::")[-1]) or re.fullmatch(cls, ns):
                base = s.getAddress()
                print(f"== {ns} vftable @ {base}")
                for i in range(0, 120):
                    p = mem.getLong(base.add(i * 8)) & 0xFFFFFFFFFFFFFFFF
                    if not (0x140000000 <= p < 0x143000000):
                        break
                    f = self.fm.getFunctionAt(self.af.getAddress(hex(p)))
                    print(f"  [{i:3}] +0x{i*8:03x}  {p:x}  {f.getName() if f else ''}")

    def cmd_find(self, rx):
        r = re.compile(rx)
        for s in self.st.getAllSymbols(True):
            n = s.getName(True)
            if r.search(n):
                print(f"{s.getAddress()}  {s.getSymbolType()}  {n}")

    def run(self, argv):
        getattr(self, "cmd_" + argv[0])(*argv[1:])

    def serve(self):
        srv = socket.create_server(("127.0.0.1", PORT))
        print(f"gdec server on 127.0.0.1:{PORT}", flush=True)
        while True:
            conn, _ = srv.accept()
            with conn:
                try:
                    with conn.makefile("rb") as f:  # (an open makefile keeps the socket from closing)
                        argv = json.loads(f.readline())
                    if argv == ["quit"]:
                        conn.sendall(b"bye\n")
                        break
                    out = io.StringIO()
                    with contextlib.redirect_stdout(out):
                        try:
                            self.run(argv)
                        except Exception as e:  # noqa: BLE001 - report to the caller, keep serving
                            print(f"// error: {type(e).__name__}: {e}")
                    conn.sendall(out.getvalue().encode("utf-8"))
                    conn.shutdown(socket.SHUT_WR)
                except Exception as e:  # noqa: BLE001
                    print("request failed:", e, flush=True)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if sys.argv[1] != "serve":
        out = via_server(sys.argv[1:])
        if out is not None:
            sys.stdout.write(out)
            sys.exit(0)
    g = Ghidra()
    try:
        if sys.argv[1] == "serve":
            g.serve()
        else:
            g.run(sys.argv[1:])
    finally:
        g.proj.close()
