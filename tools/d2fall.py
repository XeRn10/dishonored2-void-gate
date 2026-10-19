"""Find the player's accumulated fall height (m_accumulatedFallHeight at +0x88, m_lastFrameOrigin at +0x8C of some
player-side struct): sample every object reachable from the player entity (2 pointer levels) while the player jumps
down from heights, then report float fields that are 0 on the ground, grow while falling and are followed by a vec3
that tracks the player's origin.

  py d2fall.py [seconds]      (writes d2fall_samples.npz next to this script, prints candidates)
"""
import struct
import sys
import time

import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from d2live import Proc, find_pid  # noqa: E402

SPAN = 0x400


def main():
    secs = float(sys.argv[1]) if len(sys.argv) > 1 else 90
    p = Proc(find_pid())
    g = p.q(0x142710D68)
    pl = p.q(g + 0x312FF8)

    def ptrs(a, span=SPAN):
        for c in range(0, span, 0x200):  # small reads: a big one fails as a whole at an unmapped page
            b = p.read(a + c, 0x200) or b""
            for i in range(0, len(b) - 7, 8):
                q = struct.unpack_from("<Q", b, i)[0]
                if 0x10000000 < q < 0x7FF000000000 and not (0x140000000 <= q < 0x145000000) and q % 8 == 0:
                    yield q

    roots = {pl} | set(p.find_qword(0x141EE9AF8, limit=4))  # player entity + arkCpntPlayerPhysics
    objs = set(roots)
    lvl1 = set()
    for r in roots:
        lvl1 |= set(ptrs(r, 0x2000 if r == pl else SPAN))
    objs |= lvl1
    for o in list(lvl1):
        objs |= set(ptrs(o))
    objs = sorted(o for o in objs if p.read(o, SPAN))
    print(len(objs), "objects; sampling", secs, "s -- jump down from heights now")
    ts, zs, data = [], [], []
    t0 = time.perf_counter()
    while time.perf_counter() - t0 < secs:
        zb = p.read(pl + 0x308, 4)
        if not zb or p.q(g + 0x312FF8) != pl:  # the player was replaced (load, death): stop with what we have
            print("player entity changed -- stopping early")
            break
        ts.append(time.perf_counter() - t0)
        zs.append(struct.unpack("<f", zb)[0])
        row = []
        for o in objs:
            b = p.read(o, SPAN)
            row.append(np.frombuffer(b, "<f4") if b and len(b) == SPAN else np.zeros(SPAN // 4, "<f4"))
        data.append(np.stack(row))
        time.sleep(0.02)
    if len(ts) < 10:
        print("too few samples")
        return
    ts, zs, data = np.array(ts), np.array(zs), np.stack(data)  # samples x objs x floats
    np.savez_compressed(__file__.replace(".py", "_samples.npz"), ts=ts, zs=zs, data=data, objs=np.array(objs))
    dz = np.diff(zs, prepend=zs[0])
    falling = dz < -0.02
    print(f"z range {zs.min():.2f}..{zs.max():.2f}, falling samples {falling.sum()}")
    cands = []
    for oi, o in enumerate(objs):
        for fi in range(data.shape[2] - 3):
            col = data[:, oi, fi]
            if not np.all(np.isfinite(col)) or col.max() < 1.0 or col.min() < -0.01 or col.max() > 200:
                continue
            if np.mean(col[~falling] == 0) < 0.5:   # mostly 0 when not falling
                continue
            nxt = data[:, oi, fi + 3]               # z of the vec3 right after (lastFrameOrigin.z)
            track = np.mean(np.abs(nxt - zs) < 1.0)
            cands.append((track, o, fi * 4, col.max()))
    cands.sort(reverse=True)
    for c in cands[:15]:
        print(f"  object {c[1]:#x} +{c[2]:#x}: max {c[3]:.2f}, next-vec3.z tracks origin z {c[0]*100:.0f}% of the time")


if __name__ == "__main__":
    main()
