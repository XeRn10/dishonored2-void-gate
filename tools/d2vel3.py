"""Find horizontal velocity: correlate float fields of arkCpntPlayerPhysics, its state object and the objects they
point to (2 levels) against d(origin)/dt for x and y."""
import struct
import sys
import time

import numpy as np

sys.path.insert(0, __import__("os").path.dirname(__import__("os").path.abspath(__file__)))
from d2live import Proc, find_pid  # noqa: E402

SPAN = 0x600
p = Proc(find_pid())
g = p.q(0x142710D68)
pl = p.q(g + 0x312FF8)
comp = p.find_qword(0x141EE9AF8, limit=4)[0]


def ptrs(a):
    b = p.read(a, SPAN) or b""
    out = []
    for i in range(0, len(b) - 7, 8):
        q = struct.unpack_from("<Q", b, i)[0]
        if 0x10000000 < q < 0x7FF000000000 and not (0x140000000 <= q < 0x145000000) and q % 16 == 0:
            out.append((i, q))
    return out


objs = {"cpnt": comp, "player": pl}
for i, q in ptrs(comp):
    objs[f"[cpnt+{i:#x}]"] = q
for i, q in ptrs(p.q(comp + 0x18)):
    objs[f"[[cpnt+0x18]+{i:#x}]"] = q
objs = {k: v for k, v in objs.items() if p.read(v, SPAN)}
print(len(objs), "objects")
ts, org, S = [], [], {k: [] for k in objs}
t0 = time.perf_counter()
while time.perf_counter() - t0 < float(sys.argv[1] if len(sys.argv) > 1 else 6):
    ts.append(time.perf_counter() - t0)
    org.append(struct.unpack("<3f", p.read(pl + 0x300, 12)))
    for k, a in objs.items():
        b = p.read(a, SPAN)
        S[k].append(np.frombuffer(b, "<f4") if b and len(b) == SPAN else np.zeros(SPAN // 4, "<f4"))
    time.sleep(0.01)
ts, org = np.array(ts), np.array(org)
d = np.gradient(org, ts, axis=0)
print("speed x", d[:, 0].min(), d[:, 0].max(), "y", d[:, 1].min(), d[:, 1].max())
for ax in (0, 1):
    res = []
    for k, arr in S.items():
        a = np.stack(arr)
        for j in range(a.shape[1]):
            col = a[:, j]
            if not np.all(np.isfinite(col)) or np.ptp(col) < 0.5 or np.abs(col).max() > 50:
                continue
            c = np.corrcoef(col, d[:, ax])[0, 1]
            if np.isfinite(c):
                res.append((abs(c), c, k, j * 4, col.min(), col.max()))
    res.sort(reverse=True)
    print("axis", "xy"[ax])
    for r in res[:8]:
        print(f"  corr {r[1]:+.3f}  {r[2]}+{r[3]:#x}  range {r[4]:.2f}..{r[5]:.2f}")
