"""Find the player's velocity in live memory: sample arkCpntPlayerPhysics (and the objects it points to) while the
player jumps, then match float fields against the derivative of the player's origin z.

  py d2vel.py [seconds]
"""
import struct
import sys
import time

import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from d2live import Proc, find_pid  # noqa: E402

VT_PLAYER_PHYSICS = 0x141EE9AF8
G_GAME = 0x142710D68
SPAN = 0x800


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 4.0
    p = Proc(find_pid())
    g = p.q(G_GAME)
    pl = p.q(g + 0x312FF8)
    comps = p.find_qword(VT_PLAYER_PHYSICS, limit=8)
    print("player", hex(pl), "arkCpntPlayerPhysics candidates", [hex(c) for c in comps])
    comps = [c for c in comps if p.q(c + 0x10) == pl] or comps
    comp = comps[0]
    print("using", hex(comp), "owner", hex(p.q(comp + 0x10) or 0))
    # objects: the component plus every heap object it points to (first level)
    blob = p.read(comp, SPAN)
    objs = {"cpnt": comp}
    for i in range(0, SPAN, 8):
        q = struct.unpack_from("<Q", blob, i)[0]
        if 0x10000000 < q < 0x7FF000000000 and not (0x140000000 <= q < 0x145000000) and q % 8 == 0:
            if p.read(q, SPAN):
                objs[f"[cpnt+{i:#x}]"] = q
    print(len(objs), "objects sampled")
    samples, zs, ts = {k: [] for k in objs}, [], []
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < secs:
        ts.append(time.perf_counter() - t0)
        zs.append(struct.unpack("<f", p.read(pl + 0x308, 4))[0])
        for k, a in objs.items():
            b = p.read(a, SPAN)
            samples[k].append(np.frombuffer(b, dtype="<f4") if b and len(b) == SPAN else np.zeros(SPAN // 4, "<f4"))
        time.sleep(0.01)
    ts, zs = np.array(ts), np.array(zs)
    dz = np.gradient(zs, ts)
    print(f"{len(ts)} samples, z range {zs.min():.2f}..{zs.max():.2f}, dz/dt range {dz.min():.2f}..{dz.max():.2f}")
    if np.ptp(zs) < 0.3:
        print("player did not move vertically -- jump while sampling")
        return
    best = []
    for k, arr in samples.items():
        a = np.stack(arr)  # samples x floats
        for j in range(a.shape[1]):
            col = a[:, j]
            if not np.all(np.isfinite(col)) or np.ptp(col) < 0.5 or np.abs(col).max() > 100:
                continue
            c = np.corrcoef(col, dz)[0, 1]
            if np.isfinite(c):
                best.append((abs(c), c, k, j * 4, col.min(), col.max()))
    best.sort(reverse=True)
    for b in best[:15]:
        print(f"  corr {b[1]:+.3f}  {b[2]}+{b[3]:#x}  range {b[4]:.2f}..{b[5]:.2f}")


if __name__ == "__main__":
    main()
