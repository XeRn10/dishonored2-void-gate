"""Verify the player velocity vector: compare [arkCpntPlayerPhysics+0x18]+0x48.. with d(origin)/dt on all axes."""
import struct
import sys
import time

import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from d2live import Proc, find_pid  # noqa: E402

p = Proc(find_pid())
g = p.q(0x142710D68)
pl = p.q(g + 0x312FF8)
comp = p.find_qword(0x141EE9AF8, limit=4)[0]
obj = p.q(comp + 0x18)
print("physics cpnt", hex(comp), "state", hex(obj), "state vtable", hex(p.q(obj) or 0))
ts, org, vel, org2 = [], [], [], []
t0 = time.perf_counter()
while time.perf_counter() - t0 < float(sys.argv[1] if len(sys.argv) > 1 else 5):
    ts.append(time.perf_counter() - t0)
    org.append(struct.unpack("<3f", p.read(pl + 0x300, 12)))
    b = p.read(obj + 0x30, 0xE0)
    vel.append(struct.unpack("<3f", b[0x18:0x24]))   # +0x48
    org2.append(struct.unpack("<3f", b[0xC8:0xD4]))  # +0xF8
    time.sleep(0.01)
ts, org, vel, org2 = map(np.array, (ts, org, vel, org2))
d = np.gradient(org, ts, axis=0)
for k, ax in enumerate("xyz"):
    c = np.corrcoef(vel[:, k], d[:, k])[0, 1] if np.ptp(vel[:, k]) > 0 else float("nan")
    print(f"{ax}: vel range {vel[:, k].min():6.2f}..{vel[:, k].max():6.2f}  d(origin)/dt {d[:, k].min():6.2f}.."
          f"{d[:, k].max():6.2f}  corr {c:+.3f}   [+0xF8 origin? {org2[:, k].min():.2f}..{org2[:, k].max():.2f}]")
