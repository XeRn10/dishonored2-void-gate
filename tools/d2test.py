"""In-game scenario tests for the Void Gate (needs the game running a level; drives it through d2ctl).

  py d2test.py fall [speed]          hit a floor gate at <speed> m/s (default 20, like a 20 m drop), come out of
                                     a floor gate upwards, fly, land: health must not change
  py d2test.py fling [speed]         drop into a floor gate at <speed>, fly out of a wall gate (Portal fling)
  py d2test.py mc [trials] [seed]    Monte Carlo: random floating gate pairs (exit facing floor / wall / ceiling /
                                     slanted), random entry speeds, heights and offsets into a floor gate
  py d2test.py load <slot>           load a save slot and get past "press to start"

The tests work around the spot the player stands on when they start (flat floor, a few meters free around and
~8 m above; e.g. the Addermire Institute atrium at (0.7, 15.1, 20.0)), and put the player back there after each
trial. Pass = the gate teleported the player, the player came out at the other gate, landed, and lost no health.
Exit code = number of failed trials. Screenshots of failures go to scratch/.
"""
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2ctl import Game, press  # noqa: E402

SCRATCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scratch")
LOG = r"D:\Dishonored 2\dmod_d2.log"
HEALTH_TYPE = "143C064A0"  # arkCpntHealthEvolved type descriptor; current health = float at +0x20


def health(g):
    return g.cmd(f"comp {HEALTH_TYPE} 48")["f"][8]


def log_since(mark):
    with open(LOG, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()
    for i in range(len(lines) - 1, -1, -1):
        if mark in lines[i]:
            return lines[i + 1:]
    return []


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def trial(g, base, a_pos, b_pos, b_n, start_off, speed, label):
    """Floor gate A at a_pos, gate B at b_pos facing b_n; the player put at A + start_off and thrown down at
    `speed`. Returns (ok, details)."""
    g.cmd("clear")
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    time.sleep(0.6)
    hp0 = health(g)
    g.cmd(f"gate 0 {a_pos[0]:.3f} {a_pos[1]:.3f} {a_pos[2]:.3f} 0 0 1")
    g.cmd(f"gate 1 {b_pos[0]:.3f} {b_pos[1]:.3f} {b_pos[2]:.3f} {b_n[0]:.3f} {b_n[1]:.3f} {b_n[2]:.3f}")
    mark = f"trial {label} {time.time():.3f}"
    g.cmd("log " + mark)
    s = add(a_pos, start_off)
    g.cmd(f"tp {s[0]:.3f} {s[1]:.3f} {s[2]:.3f} 90 0")
    time.sleep(0.12)  # (the teleport lands a frame later)
    if speed:
        g.cmd(f"vz {-speed:.2f}")
    t0 = time.time()
    tp_done = False
    while time.time() - t0 < 3.0:
        st = g.cmd("state")
        # the module sets exitGate = 1 when it sends the player out of B (a fast exit can leave B behind quickly)
        if st.get("exitGate") == 1 or ("feet" in st and math.dist(st["feet"], b_pos) < 2.6):
            tp_done = True
            break
        time.sleep(0.03)
    t_tp = time.time() - t0
    g.cmd("clear")  # no loop between the gates: whatever the exit did, fall from there
    det = {"t_tp": round(t_tp, 2)}
    if not tp_done:
        det.update(why="no teleport", feet=st.get("feet"))
        return False, det
    peak, still, end = 0.0, None, time.time() + 15
    while time.time() < end:  # until standing still for 0.6 s
        st = g.cmd("state")
        peak = max(peak, st.get("fallHeight", 0.0))
        if abs(st.get("vz", 9)) < 0.3:
            still = still or time.time()
            if time.time() - still > 0.6:
                break
        else:
            still = None
        time.sleep(0.04)
    time.sleep(0.3)
    hp1 = health(g)
    lines = log_since(mark)
    det.update(hp=(round(hp0, 1), round(hp1, 1)), peakFall=round(peak, 2),
               landed=[ln.split("] ", 1)[1] for ln in lines if "zeroed" in ln][-1:],
               momentum=[ln.split("] ", 1)[1] for ln in lines if "momentum" in ln][:1])
    # where the teleport really put the player vs where it was sent ("after teleport: feet (..), wanted (..)")
    for ln in lines:
        if "after teleport: feet" in ln:
            nums = [float(x) for x in ln.replace("(", " ").replace(")", " ").replace(",", " ").split()
                    if x.replace(".", "", 1).replace("-", "", 1).isdigit()]
            if len(nums) >= 6:  # horizontal only: 0.3 s after the teleport a vertical exit speed has moved it
                det["arrivalErr"] = round(math.dist(nums[-6:-4], nums[-3:-1]), 2)
            break
    # (checked 0.3 s after the teleport: a horizontal exit velocity has carried the player that far meanwhile)
    allowed = 2.0
    for ln in det["momentum"]:
        try:
            out = [float(x) for x in ln.split("out (")[1].split(")")[0].split()]
            allowed += 0.4 * math.hypot(out[0], out[1])
        except (IndexError, ValueError):
            pass
    if det.get("arrivalErr", 0) > allowed:
        det["why"] = f"arrived far from the exit (allowed {allowed:.1f} m)"
        return False, det
    if hp1 < hp0 - 0.01:
        det["why"] = "lost health"
        return False, det
    if hp1 <= 0:
        det["why"] = "dead"
        return False, det
    return True, det


def base_point(g):
    st = g.cmd("state")
    if "feet" not in st:
        sys.exit("no player")
    return tuple(st["feet"])


def test_fall(g, speed=20.0):
    base = base_point(g)
    a = add(base, (0, 3, 0))
    b = add(base, (0, -3, 0))
    ok, det = trial(g, base, a, b, (0, 0, 1), (0.1, -0.1, 1.2), speed, f"fall{speed:g}")
    print(f"floor gate at {speed:g} m/s -> floor exit upwards:", "OK" if ok else "FAIL",
          json.dumps(det, ensure_ascii=False))
    if not ok:
        print("  screenshot:", g.shot(os.path.join(SCRATCH, "fail_fall.png")))
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    return 0 if ok else 1


def test_fling(g, speed=15.0):
    """Portal fling: drop into a floor gate at `speed`, come out of a wall gate facing +y: the player must fly
    out horizontally (land well past the exit) and lose no health."""
    base = base_point(g)
    a = add(base, (0, 2, 2.0))      # floor gate, floating
    b = add(base, (0, 6, 3.0))      # wall gate facing +y (into the open hall)
    g.cmd("clear")
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    time.sleep(0.6)
    hp0 = health(g)
    g.cmd(f"gate 0 {a[0]} {a[1]} {a[2]} 0 0 1")
    g.cmd(f"gate 1 {b[0]} {b[1]} {b[2]} 0 1 0")
    mark = f"trial fling {time.time():.3f}"
    g.cmd("log " + mark)
    g.cmd(f"tp {a[0]:.3f} {a[1]:.3f} {a[2] + 2.5:.3f} 90 60")
    time.sleep(0.12)
    g.cmd(f"vel 0 0 {-speed}")
    t0, went = time.time(), False
    while time.time() - t0 < 3:
        if g.cmd("state").get("exitGate") == 1:
            went = True
            break
        time.sleep(0.02)
    g.cmd("clear")
    s = landed(g) if went else None
    hp1 = health(g)
    lines = log_since(mark)
    mom = [ln.split("] ", 1)[1] for ln in lines if "momentum" in ln][:1]
    flew = s and s["feet"][1] - b[1]
    ok = went and s and flew > 3.0 and hp1 >= hp0 - 0.01
    print(f"fling at {speed:g} m/s:", "OK" if ok else "FAIL",
          json.dumps({"teleported": went, "flew_past_exit_m": round(flew, 2) if flew else None,
                      "hp": [hp0, hp1], "momentum": mom}, ensure_ascii=False))
    if not ok:
        print("  screenshot:", g.shot(os.path.join(SCRATCH, "fail_fling.png")))
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    return 0 if ok else 1


def test_walk(g):
    """Walk (W held) into a wall gate standing in front of the player; come out of a wall gate facing +x."""
    base = base_point(g)
    a = add(base, (0, 2.5, 1.2))   # wall gate facing the player (-y)
    b = add(base, (3.0, 6.0, 1.2))  # wall gate facing +x
    g.cmd("clear")
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    time.sleep(0.6)
    g.cmd(f"gate 0 {a[0]} {a[1]} {a[2]} 0 -1 0")
    g.cmd(f"gate 1 {b[0]} {b[1]} {b[2]} 1 0 0")
    mark = f"trial walk {time.time():.3f}"
    g.cmd("log " + mark)
    press(0x57, hold=1.6)  # W
    time.sleep(0.4)
    s = g.cmd("state")
    lines = log_since(mark)
    went = any("went into gate A" in ln for ln in lines)
    # where the module sent the player ("teleport from (..) to feet (x y z)"): in front of B along its normal
    # (where the player ends up also depends on what stands there: a pillar pushed it back once)
    out_x = None
    for ln in lines:
        if "teleport from" in ln and "to feet (" in ln:
            out_x = float(ln.split("to feet (")[1].split()[0]) - b[0]
            break
    ok = went and out_x is not None and out_x > 0.3
    print("walk into a wall gate:", "OK" if ok else "FAIL",
          json.dumps({"wentIn": went, "past_exit_along_normal_m": out_x and round(out_x, 2),
                      "feet": s.get("feet"), "momentum": [ln.split("] ", 1)[1] for ln in lines if "momentum" in ln]},
                     ensure_ascii=False))
    if not ok:
        print("  screenshot:", g.shot(os.path.join(SCRATCH, "fail_walk.png")))
    g.cmd("clear")
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    return 0 if ok else 1


def landed(g, timeout=12):
    still, end = None, time.time() + timeout
    while time.time() < end:
        s = g.cmd("state")
        if abs(s.get("vz", 9)) < 0.3 and abs(sum(v * v for v in s.get("vel", (0, 0, 0)))) < 0.1:
            still = still or time.time()
            if time.time() - still > 0.5:
                return s
        else:
            still = None
        time.sleep(0.04)
    return g.cmd("state")


def random_exit(rng):
    kind = rng.choice(["floor", "wall", "wall", "ceiling", "slant"])
    if kind == "floor":
        return kind, (0.0, 0.0, 1.0)
    if kind == "ceiling":
        return kind, (0.0, 0.0, -1.0)
    a = rng.uniform(-math.pi, math.pi)
    e = 0.0 if kind == "wall" else rng.uniform(0.2, 1.2)
    return kind, (math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e))


def test_mc(g, trials=12, seed=1):
    rng = random.Random(seed)
    base = base_point(g)
    fails, stats = 0, {}
    for k in range(trials):
        a = add(base, (rng.uniform(-1, 1), rng.uniform(2.5, 4), rng.uniform(0, 3)))  # floor gate, maybe floating
        kind, n = random_exit(rng)
        b = add(base, (rng.uniform(-2, 2), rng.uniform(-5, -3), rng.uniform(2.2, 4.5)))
        h = rng.uniform(0.8, 3.0)
        r, t = rng.uniform(0, 0.55), rng.uniform(-math.pi, math.pi)
        speed = rng.choice([0, 0, rng.uniform(4, 30)])
        ok, det = trial(g, base, a, b, n, (r * math.cos(t), r * math.sin(t), h), speed, f"mc{k}")
        stats.setdefault(kind, [0, 0])[0 if ok else 1] += 1
        print(f"trial {k:2}: exit {kind:7} drop {h:3.1f} m + {speed:4.1f} m/s, off {r:.2f} ->",
              "OK" if ok else "FAIL", json.dumps(det, ensure_ascii=False))
        if not ok:
            fails += 1
            print("  screenshot:", g.shot(os.path.join(SCRATCH, f"fail_mc{k}.png")))
    g.cmd("clear")
    g.cmd(f"tp {base[0]} {base[1]} {base[2] + 0.05} 90 0")
    print("by exit kind [ok, fail]:", stats)
    return fails


def select_voidgate(g):
    """Power wheel (CapsLock held), mouse up to 12 o'clock, where the user's wheel has the Void Gate; release.
    True when the Void Gate's own update starts (module log)."""
    import ctypes
    from d2ctl import focus_game
    u = ctypes.windll.user32
    focus_game()
    mark = f"select {time.time():.3f}"
    g.cmd("log " + mark)
    vk, sc = 0x14, u.MapVirtualKeyW(0x14, 0)
    u.keybd_event(vk, sc, 0, 0)
    time.sleep(0.8)
    for _ in range(6):
        u.mouse_event(0x1, -8, -50, 0, 0)
        time.sleep(0.05)
    time.sleep(0.4)
    u.keybd_event(vk, sc, 2, 0)
    time.sleep(1.0)
    return any("Void Gate update" in ln for ln in log_since(mark))


def test_aim(g, frames=8):
    """Aim the Void Gate (right mouse held) at the far wall of the atrium; screenshots of the marker, per-frame
    aim log; then release (casts a gate). Crops of the marker -> scratch/aim_strip.png"""
    from PIL import Image
    from d2ctl import mouse, focus_game
    g.cmd("clear")
    g.cmd("tp 0.7 15.1 20.05 90 25")
    time.sleep(0.8)
    focus_game()
    mark = f"aim {time.time():.3f}"
    g.cmd("log " + mark)
    g.cmd("aimlog 3")
    t0 = time.time()
    mouse("right", True)
    shots = []
    for k in range(frames):
        time.sleep(0.12)
        shots.append((round(time.time() - t0, 2), g.shot(os.path.join(SCRATCH, f"aim{k}.png"))))
    mouse("right", False)
    ims = [Image.open(p) for _, p in shots]
    w, h = ims[0].size
    box = (int(w * 0.40), int(h * 0.30), int(w * 0.60), int(h * 0.55))
    crops = [im.crop(box) for im in ims]
    cw, ch = crops[0].size
    strip = Image.new("RGB", (cw * len(crops), ch))
    for i, c in enumerate(crops):
        strip.paste(c, (i * cw, 0))
    out = os.path.join(SCRATCH, "aim_strip.png")
    strip.save(out)
    aims = [ln for ln in log_since(mark) if "aim:" in ln]
    print("frames at", [t for t, _ in shots], "->", out, "| aim log lines:", len(aims))
    return 0


def test_view(g):
    """See-through window (put by the module on the gate on screen): gate A on a wall in front of the player, gate B
    elsewhere turned 90 degrees; screenshots with the second view off and on (scratch/v_off.png, v_on.png)."""
    g.cmd("tp 0.7 15.1 20.05 90 0")
    time.sleep(0.8)
    print(g.cmd("gate 0 0.7 19.0 21.4 0 -1 0"), g.cmd("gate 1 -3.0 25.0 21.4 1 0 0"))
    g.cmd("viewon 0")
    time.sleep(1)
    g.shot(os.path.join(SCRATCH, "v_off.png"))
    g.cmd("viewon 1")
    time.sleep(2)
    print("alive:", g.cmd("ping"))
    g.shot(os.path.join(SCRATCH, "v_on.png"))
    return 0


def test_clip(g):
    """Exit gate B on a real wall (the atrium's far wall): the window with the exit-plane cut off, then on.
    -> scratch/c_pair.png (left: cut off, right: on)"""
    from PIL import Image
    g.cmd("tp 0.7 15.1 20.05 90 0")
    time.sleep(0.5)
    g.cmd("gate 0 0.7 19.0 21.4 0 -1 0")
    g.cmd("gate 1 0.70 53.94 22.99 0 -1 0")
    g.cmd("viewclip 0")
    g.cmd("viewon 1")
    time.sleep(1.5)
    a = Image.open(g.shot(os.path.join(SCRATCH, "c_off.png")))
    g.cmd("viewclip 1")
    time.sleep(1.5)
    print("alive:", g.cmd("ping"))
    b = Image.open(g.shot(os.path.join(SCRATCH, "c_on.png")))
    w, h = a.size
    box = (int(w * 0.3), int(h * 0.1), int(w * 0.7), int(h * 0.9))
    a, b = a.crop(box), b.crop(box)
    cw, ch = a.size
    o = Image.new("RGB", (cw * 2, ch))
    o.paste(a, (0, 0))
    o.paste(b, (cw, 0))
    o.resize((cw, ch // 2)).save(os.path.join(SCRATCH, "c_pair.png"))
    return 0


def test_fps(g):
    """Frame rate (module view submits per second) with no gates, the window in view, the window off, and the gates
    behind the player (no second render)."""
    def fps(sec=4.0):
        a = g.cmd("frames")
        time.sleep(sec)
        b = g.cmd("frames")
        return (b["frames"] - a["frames"]) / (b["t"] - a["t"])
    g.cmd("clear")
    g.cmd("tp 0.7 15.3 20.05 90 0")
    time.sleep(1.5)
    r = {"no gates": fps()}
    g.cmd("gate 0 0.7 19.0 21.4 0 -1 0")
    g.cmd("gate 1 -3.0 25.0 21.4 1 0 0")
    time.sleep(1.5)
    r["window in view"] = fps()
    g.cmd("viewon 0")
    time.sleep(1.0)
    r["window off (viewon 0)"] = fps()
    g.cmd("viewon 1")
    g.cmd("tp 0.7 15.3 20.05 270 0")
    time.sleep(1.5)
    r["gates behind the player"] = fps()
    g.cmd("tp 0.7 15.3 20.05 90 0")
    g.cmd("clear")
    for k, v in r.items():
        print(f"{k:28s} {v:6.1f} fps")
    return 0


def view_grid(g, modes, out="viewgrid.png"):
    """Screenshots of the window under several modes (lists of test-channel commands), cropped around the screen
    center and laid side by side -> scratch/<out>."""
    from PIL import Image, ImageDraw
    crops = []
    for label, cmds in modes:
        for c in cmds:
            g.cmd(c)
        time.sleep(1.2)
        im = Image.open(g.shot(os.path.join(SCRATCH, "vg_tmp.png")))
        w, h = im.size
        c = im.crop((int(w * 0.32), int(h * 0.12), int(w * 0.68), int(h * 1.0))).resize((int(w * 0.18), int(h * 0.44)))
        ImageDraw.Draw(c).text((6, 6), label, fill=(255, 255, 0))
        crops.append(c)
    cw, ch = crops[0].size
    grid = Image.new("RGB", (cw * len(crops), ch))
    for i, c in enumerate(crops):
        grid.paste(c, (i * cw, 0))
    path = os.path.join(SCRATCH, out)
    grid.save(path)
    print("->", path)


def test_shape(g):
    """The window's shape (the polygon of sheared pieces) up close and at an angle, on gate A and on gate B (the
    player in front of the violet one: the window moves there and shows A's side). -> scratch/shape.png"""
    g.cmd("tp 0.7 15.6 20.05 90 0")
    time.sleep(0.6)
    modes = [
        ("A front", ["gate 0 0.7 19.0 21.4 0 -1 0", "gate 1 0.70 53.94 22.99 0 -1 0"]),
        ("A close", ["tp 0.7 17.0 20.05 90 0"]),
        ("A angle", ["tp -1.6 16.6 20.05 60 0"]),
        ("B (violet)", ["tp 0.7 50.5 21.6 90 0"]),
        ("B angle", ["tp 2.6 51.2 21.6 115 0"]),
    ]
    view_grid(g, modes, "shape.png")
    print(g.cmd("ping"))
    return 0


def test_surfaces(g):
    """The window from the player at the atrium spot looking at gate A (wall, in front), with gate B on several
    kinds of places: floating mid-air, on the far wall, on the floor, on the ceiling-ish (facing down), floor A.
    -> scratch/surfaces.png"""
    g.cmd("tp 0.7 15.1 20.05 90 0")
    time.sleep(0.6)
    modes = [
        ("B air -y", ["gate 0 0.7 19.0 21.4 0 -1 0", "gate 1 0.7 32.0 21.4 0 -1 0"]),
        ("B air +x", ["gate 1 -3.0 25.0 21.4 1 0 0"]),
        ("B far wall", ["gate 1 0.70 53.94 22.99 0 -1 0"]),
        ("B floor", ["gate 1 0.7 30.0 20.02 0 0 1"]),
        ("B facing down", ["gate 1 0.7 30.0 24.0 0 0 -1"]),
        ("A floor, B wall", ["tp 0.7 15.1 20.05 90 60", "gate 0 0.7 18.0 20.02 0 0 1", "gate 1 0.70 53.94 22.99 0 -1 0"]),
    ]
    view_grid(g, modes, "surfaces.png")
    return 0


def test_tap_f(g):
    """Gates open, nothing interactive in front: a tap on F closes them (and the HUD prompt is up before)."""
    g.cmd("tp 0.7 15.1 20.05 90 0")
    time.sleep(0.5)
    g.cmd("gate 0 0.7 19.0 21.4 0 -1 0")
    g.cmd("gate 1 -3.0 25.0 21.4 1 0 0")
    time.sleep(1.5)
    g.shot(os.path.join(SCRATCH, "tapf_before.png"))
    press(0x46, hold=0.08)
    time.sleep(0.8)
    st = g.cmd("state")
    closed = not st["gates"][0]["valid"] and not st["gates"][1]["valid"]
    print("tap F closes the gates:", "OK" if closed else "FAIL", [gg["valid"] for gg in st["gates"]])
    return 0 if closed else 1


def load(g, slot):
    """LoadGame <slot>, then past "press to start". The main menu has a player of its own (its background
    scene): wait for a *new* player entity, then for live game frames (a world command gets answered)."""
    g.cmd("console win_pauseOnFocusLoss 0")  # keep running while another window has the focus
    # right after the game starts the menu's background scene is still loading (its "player" shows up late):
    # wait until the same player has been there for 4 s, so LoadGame isn't sent too early / mistaken for done
    p0, since = None, time.time()
    for _ in range(120):
        p = g.cmd("ping").get("playerPtr")
        if p != p0:
            p0, since = p, time.time()
        elif p and int(p, 16) and time.time() - since > 4:
            break
        time.sleep(0.5)
    g.cmd(f"console LoadGame {slot}")
    for _ in range(180):
        time.sleep(1)
        p = g.cmd("ping").get("playerPtr")
        if p and p != p0 and int(p, 16):
            break
    else:
        sys.exit("load: no new player")
    for attempt in range(20):  # loading screen until "press to start": press, then check the game runs
        time.sleep(2)
        press(0x20)
        try:
            g.cmd("vz 0", timeout=2)  # world command: answered only by a running game frame
            break
        except TimeoutError:
            continue
    time.sleep(1)
    print("loaded:", g.cmd("ping"))


def main():
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    g = Game()
    what = sys.argv[1]
    if what == "fall":
        sys.exit(test_fall(g, float(sys.argv[2]) if len(sys.argv) > 2 else 20.0))
    if what == "tapf":
        sys.exit(test_tap_f(g))
    if what == "shape":
        sys.exit(test_shape(g))
    if what == "surfaces":
        sys.exit(test_surfaces(g))
    if what == "clip":
        sys.exit(test_clip(g))
    if what == "fps":
        sys.exit(test_fps(g))
    if what == "view":
        sys.exit(test_view(g))
    if what == "select":
        print("void gate selected:", select_voidgate(g))
        return
    if what == "aim":
        sys.exit(test_aim(g))
    if what == "walk":
        sys.exit(test_walk(g))
    if what == "fling":
        sys.exit(test_fling(g, float(sys.argv[2]) if len(sys.argv) > 2 else 15.0))
    if what == "mc":
        sys.exit(test_mc(g, int(sys.argv[2]) if len(sys.argv) > 2 else 12,
                         int(sys.argv[3]) if len(sys.argv) > 3 else 1))
    if what == "load":
        load(g, int(sys.argv[2]))
        return
    sys.exit(__doc__)


if __name__ == "__main__":
    main()
