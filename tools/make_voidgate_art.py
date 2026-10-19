"""Draws the Void Gate journal art in the style of the stock Dishonored 2 power icons and writes .bimage7 files.

Style notes (measured on stock icons, media/d2_powers): silver glyph with mottled watercolor/metal texture
(~(183,196,201), shards along the edge), lavender bracket arcs with a saw-tooth inner edge and a light-to-dark
gradient (~(98..122, 108..127, 146..177)), thin dark ink edges, transparent background.
Sizes: power icon 148x148, upgrade icons 120x120, header 620x224 (all BC3).

Usage: py make_voidgate_art.py            -> previews in d2mods/VoidGate/art, bimage7 in the mod files
"""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bimage  # noqa: E402
import bimage_write  # noqa: E402
from d2idx import Archive  # noqa: E402

MOD = r"D:/DishonoredMod/d2mods/VoidGate"
ART = os.path.join(MOD, "art")
IMG_DIR = os.path.join(MOD, "files/generated/image/models/guis/journal_powers")
SS = 4  # supersampling

LAV_LIGHT = np.array([168, 176, 228])
LAV_DARK = np.array([82, 92, 150])
SIL_LIGHT = np.array([238, 243, 245])
SIL_DARK = np.array([132, 148, 165])
INK = (26, 26, 36)


# ---------------------------------------------------------------- textures
def field(h, w, rng, sigma):
    a = Image.fromarray((rng.random((h, w)) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(sigma))
    a = np.asarray(a).astype(float)
    return (a - a.min()) / max(1e-6, a.max() - a.min())


def silver(h, w, rng):
    n = 0.55 * field(h, w, rng, 3 * SS) + 0.3 * field(h, w, rng, 1.2 * SS) + 0.15 * field(h, w, rng, 8 * SS)
    n = np.clip((n - 0.25) * 1.6, 0, 1) ** 0.8
    col = SIL_DARK[None, None, :] * (1 - n[..., None]) + SIL_LIGHT[None, None, :] * n[..., None]
    tint = field(h, w, rng, 10 * SS)[..., None]          # cool blue patches like the stock metal
    col = col * (1 - 0.12 * tint) + np.array([170, 190, 215]) * 0.12 * tint
    img = Image.fromarray(col.clip(0, 255).astype(np.uint8)).convert("RGBA")
    d = ImageDraw.Draw(img)
    for _ in range(int(h * w / (90 * SS * SS))):         # fine scratches
        x, y = rng.random() * w, rng.random() * h
        ang = rng.normal(-0.6, 0.35)
        ln = rng.uniform(3, 14) * SS
        c = int(rng.uniform(120, 250))
        d.line([(x, y), (x + math.cos(ang) * ln, y + math.sin(ang) * ln)], fill=(c, c + 6, c + 12, 255),
               width=max(1, int(SS * rng.uniform(0.3, 0.8))))
    return img


def lavender(h, w, rng, angle=0.35):
    yy, xx = np.mgrid[0:h, 0:w]
    t = (np.cos(angle) * yy + np.sin(angle) * xx) / max(h, w)
    t = np.clip(t + 0.15 * (field(h, w, rng, 4 * SS) - 0.5), 0, 1)[..., None]
    col = LAV_LIGHT * (1 - t) + LAV_DARK * t
    return Image.fromarray(col.clip(0, 255).astype(np.uint8)).convert("RGBA")


# ---------------------------------------------------------------- shapes
def wobble(rng, n, amp, harmonics=5):
    """Smooth periodic noise along the ring (low frequency), so edges look brushed rather than hairy."""
    ph = rng.uniform(0, 2 * math.pi, harmonics)
    am = rng.uniform(0.3, 1.0, harmonics) / np.arange(1, harmonics + 1)
    t = np.linspace(0, 2 * math.pi, n, endpoint=False)
    w = sum(am[k] * np.sin((k + 2) * t + ph[k]) for k in range(harmonics))
    return 1 + amp * w / np.abs(w).max()


def ring_polygon(cx, cy, rx, ry, rot, thick, phase, rng, jag=0.04, spikes=7, spike_len=0.18, n=360):
    """Brush-like elliptical ring: thickness varies around (thick..thin), smooth wobble, a few shards."""
    outer, inner = [], []
    spike_at = rng.uniform(0, 2 * math.pi, spikes)
    wob = wobble(rng, n, jag)
    for i in range(n):
        t = 2 * math.pi * i / n
        tk = thick * (0.25 + 0.75 * (0.5 + 0.5 * math.cos(t - phase)) ** 1.3)
        j = wob[i]
        s = 0
        for a in spike_at:                                   # triangular shards on the outer edge
            dd = math.atan2(math.sin(t - a), math.cos(t - a))
            s = max(s, spike_len * max(0, 1 - abs(dd) / 0.09))
        ro = (1 + s) * j
        ri = max(0.0, 1 - tk / min(rx, ry))
        for lst, r in ((outer, ro), (inner, ri)):
            x, y = rx * r * math.cos(t), ry * r * math.sin(t)
            lst.append((cx + x * math.cos(rot) - y * math.sin(rot), cy + x * math.sin(rot) + y * math.cos(rot)))
    return outer, inner


def ring_mask(size, *args, **kw):
    outer, inner = ring_polygon(*args, **kw)
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    d.polygon(outer, fill=255)
    d.polygon(inner, fill=0)
    return m


def bracket_mask(size, cx, cy, r, a0, a1, thick, rng, teeth=9):
    """Lavender bracket arc: tapered at both ends, saw-tooth inner edge."""
    pts_out, pts_in = [], []
    n = 120
    for i in range(n + 1):
        u = i / n
        a = a0 + (a1 - a0) * u
        taper = math.sin(math.pi * u) ** 0.7
        tooth = (u * teeth) % 1.0
        tk = thick * taper * (0.55 + 0.45 * tooth)
        pts_out.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        pts_in.append((cx + (r - tk) * math.cos(a), cy + (r - tk) * math.sin(a)))
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).polygon(pts_out + pts_in[::-1], fill=255)
    return m


def compose(layers, size):
    """layers: list of (fill_image, mask). Adds a thin dark ink edge under each layer."""
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    for fill, mask in layers:
        edge = mask.filter(ImageFilter.MaxFilter(2 * (SS // 2) + 1))
        ink = Image.new("RGBA", size, INK + (0,))
        ink.putalpha(edge.point(lambda v: int(v * 0.75)))
        out = Image.alpha_composite(out, ink)
        f = fill.copy()
        f.putalpha(mask)
        out = Image.alpha_composite(out, f)
    return out


def finish(img, size):
    return img.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- icons
def power_icon(seed=7):
    S = 148 * SS
    rng = np.random.default_rng(seed)
    size = (S, S)
    c = S / 2
    lay = []
    lav = lavender(S, S, rng)
    lay.append((lav, ImageChops.lighter(
        bracket_mask(size, c, c, 0.47 * S, math.radians(118), math.radians(232), 0.095 * S, rng, teeth=7),
        bracket_mask(size, c, c, 0.47 * S, math.radians(-58), math.radians(52), 0.095 * S, rng, teeth=7))))
    sil = silver(S, S, rng)
    rot = math.radians(-18)
    outer = ring_mask(size, c - 0.02 * S, c + 0.01 * S, 0.27 * S, 0.37 * S, rot, 0.085 * S, math.radians(200), rng,
                      spikes=6, spike_len=0.16)
    inner = ring_mask(size, c + 0.035 * S, c - 0.035 * S, 0.12 * S, 0.17 * S, rot, 0.05 * S, math.radians(40), rng,
                      spikes=3, spike_len=0.22)
    # two swirl strokes falling from the outer gate into the inner one (the passage)
    sw = Image.new("L", size, 0)
    d = ImageDraw.Draw(sw)
    for k, (r0, a0) in enumerate(((0.21, 150), (0.2, -30))):
        pts = []
        for i in range(40):
            u = i / 39
            r = (r0 - 0.08 * u) * S
            a = math.radians(a0 + 95 * u)
            pts.append((c + r * math.cos(a) * 0.75, c + r * math.sin(a)))
        d.line(pts, fill=255, width=int(0.022 * S), joint="curve")
    lay.append((sil, ImageChops.lighter(ImageChops.lighter(outer, inner), sw)))
    return finish(compose(lay, size), 148)


# Upgrade icons are flatter than power icons: pale mint subject + flat lavender accents with tapered streaks.
MINT = (214, 234, 229)
LAV_FLAT = (118, 126, 188)


def flat(size, rgb):
    return Image.new("RGBA", size, rgb + (255,))


def streak(d, x0, y0, x1, y1, w):
    """Speed line: thin at (x0,y0), full width at (x1,y1), like the stock motion strokes."""
    ang = math.atan2(y1 - y0, x1 - x0)
    nx, ny = -math.sin(ang), math.cos(ang)
    d.polygon([(x0, y0), (x1 + nx * w / 2, y1 + ny * w / 2), (x1 + math.cos(ang) * w * 0.6, y1 + math.sin(ang) * w * 0.6),
               (x1 - nx * w / 2, y1 - ny * w / 2)], fill=255)


def arrow_head(d, x, y, w, h):
    """Stock-like notched arrow head pointing right, tip at (x, y)."""
    d.polygon([(x, y), (x - w, y - h / 2), (x - w * 0.62, y - h * 0.08), (x - w * 0.95, y),
               (x - w * 0.62, y + h * 0.08), (x - w, y + h / 2)], fill=255)


def chevron(d, x, y, w, h):
    d.polygon([(x, y), (x + w * 0.45, y), (x + w, y + h / 2), (x + w * 0.45, y + h), (x, y + h),
               (x + w * 0.55, y + h / 2)], fill=255)


def stock_icon(name):
    a = Archive()
    x = [e for p, e in a.entries.items() if p.endswith("journal_powers/" + name + ".bimage7")][0]
    return np.asarray(bimage.decode(a.read(x)).convert("RGBA")).astype(int)


def figure_mask(src_name, size, height, ox, oy, flip=False):
    """Pale silhouette (runner / lunging figure) cut out of a stock upgrade icon, scaled to `height` px."""
    src = stock_icon(src_name)
    r, g, b, al = src[..., 0], src[..., 1], src[..., 2], src[..., 3]
    # soft mask (pale, non-lavender pixels weighted by alpha) so the upscaled silhouette stays smooth
    soft = np.clip((g - 150) / 50.0, 0, 1) * np.clip((g - b + 20) / 30.0, 0, 1) * (al / 255.0)
    im = Image.fromarray((soft * 255).astype(np.uint8), "L")
    im = im.crop(im.point(lambda v: 255 if v > 60 else 0).getbbox())
    if flip:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    w, h = im.size
    im = im.resize((max(1, int(w * height / h)), int(height)), Image.BICUBIC)
    im = im.filter(ImageFilter.GaussianBlur(SS * 0.6)).point(lambda v: 0 if v < 90 else (255 if v > 150 else (v - 90) * 4))
    m = Image.new("L", size, 0)
    m.paste(im, (int(ox), int(oy)))
    return m


def upgrade_far(seed=11):
    """Far Rift: long lavender arrows flying to a small distant gate."""
    S = 120 * SS
    rng = np.random.default_rng(seed)
    size = (S, S)
    lav = Image.new("L", size, 0)
    d = ImageDraw.Draw(lav)
    for y, x0, x1, w in ((0.3, 0.04, 0.62, 0.05), (0.7, 0.1, 0.62, 0.05)):
        streak(d, x0 * S, y * S, (x1 - 0.06) * S, y * S, w * S)
        arrow_head(d, x1 * S, y * S, 0.16 * S, 0.17 * S)
    for i, x in enumerate((0.08, 0.27)):
        chevron(d, x * S, 0.4 * S, 0.17 * S, 0.2 * S)
    gate = ring_mask(size, 0.8 * S, 0.5 * S, 0.1 * S, 0.21 * S, math.radians(-10), 0.05 * S, math.radians(180),
                     rng, jag=0.03, spikes=3, spike_len=0.25)
    return finish(compose([(flat(size, LAV_FLAT), lav), (flat(size, MINT), gate)], size), 120)


def upgrade_through(seed=13):
    """Through the Void: a runner steps into a gate and vanishes inside it."""
    S = 120 * SS
    rng = np.random.default_rng(seed)
    size = (S, S)
    gx, gy, grx, gry = 0.66 * S, 0.5 * S, 0.15 * S, 0.34 * S
    gate = ring_mask(size, gx, gy, grx, gry, math.radians(-6), 0.065 * S, math.radians(180), rng, jag=0.03,
                     spikes=4, spike_len=0.18)
    fig = figure_mask("j_pwr_bendtime_relativity_is", size, 0.62 * S, 0.3 * S, 0.18 * S)
    inside = Image.new("L", size, 0)
    ImageDraw.Draw(inside).ellipse([gx - grx * 0.8, gy - gry * 0.9, gx + grx * 0.8, gy + gry * 0.9], fill=255)
    fig = ImageChops.subtract(fig, inside)                # the half already in the Void is gone
    lav = Image.new("L", size, 0)
    d = ImageDraw.Draw(lav)
    for y, x0, x1, w in ((0.3, 0.02, 0.3, 0.04), (0.55, 0.0, 0.28, 0.05), (0.78, 0.06, 0.32, 0.035)):
        streak(d, x0 * S, y * S, x1 * S, y * S, w * S)
    return finish(compose([(flat(size, LAV_FLAT), lav), (flat(size, MINT), ImageChops.lighter(fig, gate))], size),
                  120)


def upgrade_momentum(seed=17):
    """Conservation of Momentum: the figure bursts out of a gate with speed lines."""
    S = 120 * SS
    rng = np.random.default_rng(seed)
    size = (S, S)
    gx, gy = 0.2 * S, 0.5 * S
    gate = ring_mask(size, gx, gy, 0.1 * S, 0.3 * S, math.radians(4), 0.05 * S, math.radians(0), rng, jag=0.03,
                     spikes=4, spike_len=0.2)
    fig = figure_mask("j_pwr_blink_assault_is", size, 0.45 * S, 0.2 * S, 0.24 * S)
    inside = Image.new("L", size, 0)
    ImageDraw.Draw(inside).ellipse([gx - 0.07 * S, gy - 0.26 * S, gx + 0.07 * S, gy + 0.26 * S], fill=255)
    fig = ImageChops.subtract(fig, inside)
    lav = Image.new("L", size, 0)
    d = ImageDraw.Draw(lav)
    for y, x0, x1, w in ((0.22, 0.3, 0.92, 0.05), (0.8, 0.28, 0.9, 0.05)):
        streak(d, x0 * S, y * S, (x1 - 0.07) * S, y * S, w * S)
        arrow_head(d, x1 * S, y * S, 0.15 * S, 0.15 * S)
    for y in (0.38, 0.5, 0.62):
        streak(d, 0.24 * S, y * S, 0.42 * S, y * S, 0.03 * S)
    return finish(compose([(flat(size, LAV_FLAT), lav), (flat(size, MINT), ImageChops.lighter(fig, gate))], size),
                  120)


# ---------------------------------------------------------------- header
def void_rift(size, cx, cy, rx, ry, rot, rng):
    """Painted Void rift for the header: dark ink ring, purple glow, pale mist inside."""
    W, H = size
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    glow = Image.new("L", size, 0)
    ImageDraw.Draw(glow).ellipse([cx - rx * 1.25, cy - ry * 1.2, cx + rx * 1.25, cy + ry * 1.2], fill=150)
    glow = glow.filter(ImageFilter.GaussianBlur(rx * 0.45))
    purple = Image.new("RGBA", size, (104, 84, 150, 0))
    purple.putalpha(glow.point(lambda v: int(v * 0.6)))
    layer = Image.alpha_composite(layer, purple)
    # the Void inside: deep ink with a faint violet swirl
    deep = Image.new("L", size, 0)
    ImageDraw.Draw(deep).ellipse([cx - rx * 0.86, cy - ry * 0.9, cx + rx * 0.86, cy + ry * 0.9], fill=235)
    deep = deep.filter(ImageFilter.GaussianBlur(rx * 0.12))
    dark = Image.new("RGBA", size, (16, 13, 24, 0))
    dark.putalpha(deep)
    layer = Image.alpha_composite(layer, dark)
    sw = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(sw)
    for k in range(3):
        pts = []
        a0 = rng.uniform(0, 2 * math.pi)
        for i in range(30):
            u = i / 29
            r = 0.75 - 0.6 * u
            a = a0 + 3.2 * u
            pts.append((cx + rx * r * math.cos(a), cy + ry * r * math.sin(a)))
        d.line(pts, fill=(118, 102, 170, 110), width=2, joint="curve")
    layer = Image.alpha_composite(layer, sw.filter(ImageFilter.GaussianBlur(1.2)))
    ring = ring_mask(size, cx, cy, rx, ry, rot, rx * 0.22, rng.uniform(0, 6.28), rng, jag=0.06, spikes=9,
                     spike_len=0.3)
    ink = Image.new("RGBA", size, (14, 12, 20, 0))
    ink.putalpha(ring.filter(ImageFilter.GaussianBlur(1.2)))
    layer = Image.alpha_composite(layer, ink)
    rim = ring_mask(size, cx, cy, rx * 0.92, ry * 0.93, rot, rx * 0.06, rng.uniform(0, 6.28), rng, jag=0.03,
                    spikes=0)
    lav = Image.new("RGBA", size, (176, 160, 232, 0))
    lav.putalpha(rim.point(lambda v: int(v * 0.85)).filter(ImageFilter.GaussianBlur(1.5)))
    layer = Image.alpha_composite(layer, lav)
    d = ImageDraw.Draw(layer)                                # ink flecks thrown off the rift
    for _ in range(40):
        a = rng.uniform(0, 2 * math.pi)
        r = rng.uniform(1.05, 1.6)
        x, y = cx + rx * r * math.cos(a), cy + ry * r * math.sin(a)
        ln = rng.uniform(3, 14)
        d.line([(x, y), (x + ln * math.cos(a), y + ln * math.sin(a))], fill=(14, 12, 20, int(rng.uniform(90, 200))),
               width=int(rng.uniform(1, 3)))
    return layer


def header(who="emily", seed=23):
    """Journal header; the game swaps the emily_ prefix for corvo_ when playing Corvo, so both are needed."""
    a = Archive()
    x = [e for p, e in a.entries.items() if p.endswith(f"journal_powers/{who}_header_p_blink_is.bimage7")][0]
    base = bimage.decode(a.read(x)).convert("RGBA")
    size = base.size
    rng = np.random.default_rng(seed)
    art = base.copy()
    art = Image.alpha_composite(art, void_rift(size, 205, 98, 30, 52, math.radians(8), rng))    # entrance, left wall
    art = Image.alpha_composite(art, void_rift(size, 452, 96, 30, 58, math.radians(-6), rng))   # exit, she comes out of it
    # match the stock grade: desaturate slightly and add paper grain, keep the original torn-edge alpha
    rgb = np.asarray(art).astype(float)
    grey = rgb[..., :3].mean(-1, keepdims=True)
    rgb[..., :3] = rgb[..., :3] * 0.85 + grey * 0.15
    rgb[..., :3] += (rng.normal(0, 4, rgb.shape[:2]))[..., None]
    out = Image.fromarray(rgb.clip(0, 255).astype(np.uint8), "RGBA")
    out.putalpha(base.getchannel("A"))
    return out


# ---------------------------------------------------------------- power wheel / HUD icon
# lib_wheel.iggy subfile 8 = texture table of lib_wheel.raw.iggytex: u16 count, u16 ?, u32 total size, 8 zero bytes,
# then 12-byte records {u32 offset, u8 format (0x0D BC7, 0x00 RGBA8), u8 1, u16 w, u16 h, u16 bitmap id}.
# Texture #14 (id 57, 92x104 BC7) is the Outsider's Timepiece (G_TIMESHIFT_DEVICE), the icon slot Void Gate uses.
WHEEL_SLOT = 14
ICE = (226, 239, 247)


def wheel_table(iggy):
    import struct
    magic, = struct.unpack_from("<I", iggy, 0)
    assert magic == 0xED0A6749
    n = struct.unpack_from("<I", iggy, 0x1C)[0]
    for i in range(n):
        sid, size, _, off = struct.unpack_from("<IIII", iggy, 0x20 + 16 * i)
        if sid == 8:
            sub = iggy[off:off + size]
            cnt = struct.unpack_from("<H", sub, 0)[0]
            return [struct.unpack_from("<IBBHHH", sub, 0x10 + 12 * k) for k in range(cnt)]
    raise ValueError("no texture table")


def hatch(size, spacing, width, angle=45):
    m = Image.new("L", size, 0)
    d = ImageDraw.Draw(m)
    w, h = size
    for k in range(-h, w + h, spacing):
        d.line([(k, 0), (k + h, h)], fill=255, width=width)
    return m


def wheel_icon(w=92, h=104, seed=31):
    """Wheel style of the bright power icons: ink outline, icy white fill with blue shading, lavender brackets."""
    W, H = w * SS, h * SS
    rng = np.random.default_rng(seed)
    size = (W, H)
    cx, cy = W * 0.5, H * 0.5
    outer = ring_mask(size, cx - 0.01 * W, cy + 0.01 * H, 0.3 * W, 0.4 * H, math.radians(-16), 0.085 * W,
                      math.radians(200), rng, jag=0.03, spikes=5, spike_len=0.14)
    inner = ring_mask(size, cx + 0.04 * W, cy - 0.03 * H, 0.13 * W, 0.18 * H, math.radians(-16), 0.065 * W,
                      math.radians(30), rng, jag=0.03, spikes=2, spike_len=0.2)
    glyph = ImageChops.lighter(outer, inner)
    br = ImageChops.lighter(
        bracket_mask(size, cx, cy, 0.49 * min(W, H), math.radians(122), math.radians(228), 0.1 * W, rng, teeth=6),
        bracket_mask(size, cx, cy, 0.49 * min(W, H), math.radians(-52), math.radians(48), 0.1 * W, rng, teeth=6))
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    ink = Image.new("RGBA", size, (24, 22, 30, 0))
    ink.putalpha(ImageChops.lighter(glyph, br).filter(ImageFilter.MaxFilter(4 * SS // 2 * 2 + 1)))
    out = Image.alpha_composite(out, ink)
    lav = Image.new("RGBA", size, (128, 138, 222, 0))
    lav.putalpha(br)
    out = Image.alpha_composite(out, lav)
    ice = Image.new("RGBA", size, ICE + (0,))
    ice.putalpha(glyph)
    out = Image.alpha_composite(out, ice)
    # dark hatching on the shadow side of the gate (lower-left), like the stock wheel icons
    shade = Image.new("L", size, 0)
    ImageDraw.Draw(shade).ellipse([cx - 0.6 * W, cy - 0.1 * H, cx + 0.15 * W, cy + 0.7 * H], fill=255)
    hm = ImageChops.multiply(shade.filter(ImageFilter.GaussianBlur(6 * SS)), glyph)
    hl = Image.new("RGBA", size, (126, 152, 214, 0))     # cool blue shadow side, like the bright power icons
    hl.putalpha(hm.point(lambda v: int(v * 0.8)))
    out = Image.alpha_composite(out, hl)
    return out.resize((w, h), Image.LANCZOS)


def patch_wheel(icon):
    import etcpak
    import texture2ddecoder as t2d
    a = Archive()
    iggy = a.read(a.entries["ui/lib_wheel.iggy"])
    tex = bytearray(a.read(a.entries["ui/lib_wheel.raw.iggytex"]))
    off, fmt, _, w, h, tid = wheel_table(iggy)[WHEEL_SLOT]
    assert (fmt, w, h, tid) == (0x0D, 92, 104, 57), "unexpected wheel slot"
    data = etcpak.compress_bc7(np.asarray(icon.convert("RGBA")).tobytes(), w, h)
    assert len(data) == w * h
    back = np.asarray(Image.frombytes("RGBA", (w, h), t2d.decode_bc7(data, w, h), "raw", "BGRA")).astype(int)
    err = np.abs(back - np.asarray(icon.convert("RGBA")).astype(int)).mean()
    assert err < 6, f"BC7 round trip error {err:.2f}"
    tex[off:off + len(data)] = data
    dst = os.path.join(MOD, "files/ui/lib_wheel.raw.iggytex")
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, "wb").write(bytes(tex))
    print(f"wheel icon -> slot #{WHEEL_SLOT} (id {tid}) at +0x{off:x}, BC7 error {err:.2f}")


def stock_template(name):
    a = Archive()
    x = [e for p, e in a.entries.items() if p.endswith("journal_powers/" + name)][0]
    return a.read(x)


def main():
    os.makedirs(ART, exist_ok=True)
    os.makedirs(IMG_DIR, exist_ok=True)
    jobs = [
        ("j_pwr_voidgate_is", power_icon(), "j_pwr_mesmerize_is.bimage7"),
        ("j_pwr_voidgate_far_is", upgrade_far(), "j_pwr_blink_greater_is.bimage7"),
        ("j_pwr_voidgate_through_is", upgrade_through(), "j_pwr_blink_greater_is.bimage7"),
        ("j_pwr_voidgate_momentum_is", upgrade_momentum(), "j_pwr_blink_greater_is.bimage7"),
        ("emily_header_p_voidgate_is", header("emily"), "emily_header_p_blink_is.bimage7"),
        ("corvo_header_p_voidgate_is", header("corvo"), "corvo_header_p_blink_is.bimage7"),
    ]
    for name, img, tpl in jobs:
        img.save(os.path.join(ART, name + ".png"))
        open(os.path.join(IMG_DIR, name + ".bimage7"), "wb").write(bimage_write.encode(img, stock_template(tpl)))
        print("wrote", name, img.size)
    wi = wheel_icon()
    wi.save(os.path.join(ART, "wheel_voidgate.png"))
    patch_wheel(wi)


if __name__ == "__main__":
    main()
