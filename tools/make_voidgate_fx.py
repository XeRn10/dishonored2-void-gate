"""Void Gate visuals: the gate FX, derived from the Outsider's Void portal (void_house/rocks/portal/void_portal.fx).

Writes into d2mods/VoidGate/files:
  generated/decls/particledt/models/effects/gameplay/powers/voidgate/voidgate_gate_01.particledt.decl
  generated/decls/fx/models/effects/gameplay/powers/voidgate/voidgate_gate_01.fx.decl
and copies the Void portal's meshes/materials/sounds (catalog 2 only, Void map) into game1 via d2deps.

FX frame (set by voidgate.dll): x = out of the surface, z = up along a wall (row 0 of the axis = surface normal).
The gate is an oval GATE_W x GATE_H in the y/z plane.
Stock stages: 0 vortex_blur (distortion swirl mesh), 1 vortex_glow (sprites spawned on the ring mesh
spawn_portal.lwo: y +-1.85, z +-2.29), 2 vortex_glow_depth (soft glow), 3 vortex_taurus_blue (torus_1m.lwo, XY plane,
r 0.5), 4 vortex_rocks_small (Void rocks sucked in), 5/6 big rocks (dropped).
"""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2deps import Deps  # noqa: E402

MOD = r"D:/DishonoredMod/d2mods/VoidGate"
FX = "models/effects/gameplay/powers/voidgate"
NAME = FX + "/voidgate_gate_01"
VOID = "models/effects/map_specific/void_house/rocks/portal"
MAT = "models/effects/materials/gameplay/powers/voidgate"
GATE_W, GATE_H = 1.7, 2.8  # Portal-sized and a bit more (1.1 x 1.9 and 1.3 x 2.2 both read as too small)
RING_HALF_Y, RING_HALF_Z = 1.849, 2.29        # spawn_portal.lwo extents
VORTEX_HALF_Y, VORTEX_HALF_Z = 1.223, 1.529   # portal_vortex.lwo extents


def blocks(text, indent):
    """Split '<indent>item[k] = {' ... '<indent>}' blocks -> (head, [block, ...], tail)."""
    lines = text.split("\n")
    out, cur, head, tail, depth_start = [], None, [], [], None
    for ln in lines:
        if cur is None and re.match(rf"^{indent}item\[\d+\] = {{$", ln):
            cur = [ln]
            continue
        if cur is not None:
            cur.append(ln)
            if ln == f"{indent}}}":
                out.append("\n".join(cur))
                cur = None
            continue
        (tail if out else head).append(ln)
    return "\n".join(head), out, "\n".join(tail)


def vec(name, x, y, z, ind):
    t = "\t" * ind
    return (f"{t}{name} = {{\n{t}\tm_max = {{\n{t}\t\tx = {x};\n{t}\t\ty = {y};\n{t}\t\tz = {z};\n{t}\t}}\n"
            f"{t}\tm_min = {{\n{t}\t\tx = {x};\n{t}\t\ty = {y};\n{t}\t\tz = {z};\n{t}\t}}\n{t}}}")


def vec_range(name, lo, hi, ind):
    t = "\t" * ind
    return (f"{t}{name} = {{\n{t}\tm_max = {{\n{t}\t\tx = {hi[0]};\n{t}\t\ty = {hi[1]};\n{t}\t\tz = {hi[2]};\n{t}\t}}\n"
            f"{t}\tm_min = {{\n{t}\t\tx = {lo[0]};\n{t}\t\ty = {lo[1]};\n{t}\t\tz = {lo[2]};\n{t}\t}}\n{t}}}")


def planar(ind, primary=(1, 0, 0), secondary=(0, 0, 1), world=False):
    """VECTOR_PLANAR orientation. Meshes: their z goes along `primary` (the gate normal: local x), x along `secondary`.
    Sprites behave like axial billboards around a local primary axis -- flat on a wall, standing up on a floor -- so
    the floor variant gives them world axes (primary = world up) instead."""
    t = "\t" * ind
    p, s, w = primary, secondary, "true" if world else "false"

    def v3(a):
        return (f"{t}\t\t\tm_max = {{\n{t}\t\t\t\tx = {a[0]};\n{t}\t\t\t\ty = {a[1]};\n{t}\t\t\t\tz = {a[2]};\n{t}\t\t\t}}\n"
                f"{t}\t\t\tm_min = {{\n{t}\t\t\t\tx = {a[0]};\n{t}\t\t\t\ty = {a[1]};\n{t}\t\t\t\tz = {a[2]};\n{t}\t\t\t}}\n")
    return (f"{t}m_orientation = {{\n{t}\tm_mode = \"VECTOR_PLANAR\";\n"
            f"{t}\tm_primaryAxis = {{\n{t}\t\tm_value = {{\n{v3(p)}{t}\t\t}}\n{t}\t\tm_worldspace = {w};\n{t}\t}}\n"
            f"{t}\tm_viewAsOrientationAxis = false;\n"
            f"{t}\tm_secondaryAxis = {{\n{t}\t\tm_value = {{\n{v3(s)}{t}\t\t}}\n{t}\t\tm_worldspace = {w};\n{t}\t}}\n"
            f"{t}}}")


def sub_block(text, key, new, ind):
    """Replace the block '<ind tabs>key = {' ... matching '}' (first occurrence) with `new` ('' removes it)."""
    t = "\t" * ind
    m = re.search(rf"^{t}{key} = {{\n(?:.*\n)*?{t}}}\n", text, re.M)
    if not m:
        raise KeyError(key)
    return text[:m.start()] + (new + "\n" if new else "") + text[m.end():]


def set_minmax(text, key, lo, hi, ind):
    t = "\t" * ind
    m = re.search(rf"^{t}{key} = {{\n{t}\tm_max = (\S+);\n{t}\tm_min = (\S+);\n{t}}}", text, re.M)
    if not m:
        raise KeyError(key)
    return text[:m.start()] + f"{t}{key} = {{\n{t}\tm_max = {hi};\n{t}\tm_min = {lo};\n{t}}}" + text[m.end():]


def make_particles(stock, variant="gate", color=""):
    """variant: "gate" or "preview" (aim marker: rim and sparks only). color: "" = blue (gate A), "_b" = violet (B)."""
    head, st, tail = blocks(stock, "\t\t\t")
    assert len(st) == 7, len(st)
    blur, glow, _depth, torus, rocks = st[:5]  # depth glow and big rocks dropped: they read as a cloud
    sy, sz = GATE_W / 2 / RING_HALF_Y, GATE_H / 2 / RING_HALF_Z

    # 0: distortion swirl filling the oval, flat in the gate plane. Under VECTOR_PLANAR the mesh's z goes along the
    # gate normal and x along its up axis; portal_vortex.lwo is deep along x and tall along z, so it is turned 90
    # degrees about y first (as the Void portal does) and flattened -- unturned it stood 1.9 m out of a floor as a dome.
    blur = sub_block(blur, "m_initialRotation", vec("m_initialRotation", 0, 90, 0, 7), 7)
    blur = sub_block(blur, "m_initialSize",
                     vec_range("m_initialSize", (0.3, GATE_W / 2 / VORTEX_HALF_Y * 0.95, GATE_H / 2 / VORTEX_HALF_Z * 0.95),
                               (0.3, GATE_W / 2 / VORTEX_HALF_Y, GATE_H / 2 / VORTEX_HALF_Z), 7), 7)
    blur = blur.replace("\t\t\t\t\t\tm_mesh =", planar(6) + "\n\t\t\t\t\t\tm_mesh =", 1)
    blur = blur.replace(f'"{VOID}/vortex.material"', f'"{MAT}/voidgate_disto.material"')

    # 1: the glowing edge -- many small bright sparks born exactly on the oval ring (own crisp blue glow)
    edge = glow.replace('m_name = "vortex_glow"', 'm_name = "gate_edge"')
    # (v2 at 280/s, 0.05-0.09 m, 1.5 s, growing x1.5 burnt the whole gate to white: additive overdraw)
    edge = set_minmax(edge, "m_rate", 90, 90, 5)
    edge = sub_block(edge, "m_life", "\t\t\t\t\tm_life = {\n\t\t\t\t\t\tm_max = 0.5;\n\t\t\t\t\t\tm_min = 0.3;\n\t\t\t\t\t}", 5)
    edge = sub_block(edge, "m_initialLocation", "", 5)
    edge = sub_block(edge, "m_scale", vec("m_scale", 0.3, sy, sz, 6), 6)
    edge = sub_block(edge, "m_initialSize", vec_range("m_initialSize", (0.025,) * 3, (0.045,) * 3, 7), 7)
    edge = sub_block(edge, "m_sizeOverLife", "", 6)
    edge = edge.replace(f'"{VOID}/vortex_color.material"', f'"{MAT}/voidgate_spark{color}.material"')

    # 3: the torus as a bright oval rim in the gate plane (no spin: a spinning oval is a blurred circle)
    torus = sub_block(torus, "m_initialRotation", "", 7)
    torus = sub_block(torus, "m_rotationRateOverLife", "", 6)
    # VECTOR_PLANAR lays the mesh's x along the gate's up axis (seen in game: x=1.1/y=1.9 came out lying on its side)
    torus = sub_block(torus, "m_initialSize", vec("m_initialSize", GATE_H, GATE_W, 1, 7), 7)
    torus = sub_block(torus, "m_sizeOverLife", "", 6)
    torus = torus.replace("\t\t\t\t\t\tm_mesh =", planar(6) + "\n\t\t\t\t\t\tm_mesh =", 1)
    # the stock stage has no m_life: its rings never died, so every stopped gate / aim marker left them behind
    torus = re.sub(r"(\n\t\t\t\t\tm_rate = \{\n(?:.*\n)*?\t\t\t\t\t\}\n)",
                   r"\1\t\t\t\t\tm_life = {\n\t\t\t\t\t\tm_max = 1.2;\n\t\t\t\t\t\tm_min = 1.2;\n\t\t\t\t\t}\n", torus,
                   count=1)
    assert "m_life" in torus
    rim = torus.replace(f'"{VOID}/vortex_blue.material"', f'"{MAT}/voidgate_rim{color}.material"')
    if variant == "preview":
        # the aim marker must stand at once and steadily: the gate rim's rings (5/s, 1.2 s, alpha up then down)
        # took ~1.5 s to build up and kept pulsing, which read as the marker jerking. Short-lived rings at full
        # alpha, many per second (curve values are triples: in tangent, value, out tangent).
        rim = set_minmax(rim, "m_rate", 12, 12, 5)
        rim = rim.replace("m_life = {\n\t\t\t\t\t\tm_max = 1.2;\n\t\t\t\t\t\tm_min = 1.2;",
                          "m_life = {\n\t\t\t\t\t\tm_max = 0.4;\n\t\t\t\t\t\tm_min = 0.4;", 1)
        rim = sub_block(rim, "m_alphaOverLife",
                        "\t\t\t\t\t\tm_alphaOverLife = {\n\t\t\t\t\t\t\tm_curve = {\n\t\t\t\t\t\t\t\tm_maxCurve = {\n"
                        "\t\t\t\t\t\t\t\t\tm_times = {\n\t\t\t\t\t\t\t\t\t\tnum = 2;\n\t\t\t\t\t\t\t\t\t\titem[1] = 1;\n"
                        "\t\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t\t\tm_values = {\n\t\t\t\t\t\t\t\t\t\tnum = 6;\n"
                        "\t\t\t\t\t\t\t\t\t\titem[1] = 1;\n\t\t\t\t\t\t\t\t\t\titem[4] = 0.7;\n\t\t\t\t\t\t\t\t\t}\n"
                        "\t\t\t\t\t\t\t\t}\n\t\t\t\t\t\t\t}\n\t\t\t\t\t\t}", 6)
        assert "m_max = 0.4;" in rim

    # 2: the void inside: the same kind of mesh particle as the rim (meshes keep their orientation on walls and floors
    # alike; sprites stood up on floors), a 1 m quad stretched to the oval with a dark swirling soft-disc material
    void = torus.replace('m_name = "vortex_taurus_blue"', 'm_name = "gate_void"')
    void = void.replace('"models/effects/meshes/torus_1m.lwo"', '"models/effects/meshes/plane_1m_vfx.lwo"')
    void = void.replace(f'"{VOID}/vortex_blue.material"', f'"{MAT}/voidgate_void.material"')
    void = sub_block(void, "m_initialSize", vec("m_initialSize", GATE_H * 0.95, GATE_W * 0.95, 1, 7), 7)

    # 4: specks of Void rock drawn into the gate
    rocks = set_minmax(rocks, "m_rate", 14, 14, 5)
    rocks = sub_block(rocks, "m_initialLocation", "", 5)
    rocks = sub_block(rocks, "m_scale", vec("m_scale", 0.6, sy * 1.6, sz * 1.4, 6), 6)
    assert rocks.count("x = -1.5;") == 2
    rocks = rocks.replace("x = -1.5;", "x = -0.35;")
    for axis in "xyz":
        rocks = rocks.replace(f"{axis} = 0.200000003;\n", f"{axis} = 0.04;\n")

    # (the distortion swirl `blur` is dropped: turned or not, it stood out of floors or lay across walls)
    stages = [rim, edge] if variant == "preview" else [void, rim, edge, rocks]
    stages = [re.sub(r"^\t\t\titem\[\d+\] = \{", f"\t\t\titem[{i}] = {{", s, count=1) for i, s in enumerate(stages)]
    head = head.replace("num = 7;", f"num = {len(stages)};")
    head = re.sub(r"x = 15;|y = 15;|z = 15;", lambda m: m.group(0).replace("15", "3"), head)
    head = re.sub(r"x = -15;|y = -15;|z = -15;", lambda m: m.group(0).replace("-15", "-3"), head)
    return head + "\n" + "\n".join(stages) + "\n" + tail


def write(rel, text):
    path = os.path.join(MOD, "files", rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="latin1", newline="\n") as f:
        f.write(text)
    print("wrote", rel, len(text))


def materials(d):
    """Own materials: color comes only from color_Adjust over a grey mask, so it can be anything."""
    g, e = d.lookup(f"{VOID}/vortex_blue.material")[0]
    blue = d.read(g, e).decode("latin1").replace("\r\n", "\n")
    rim = re.sub(r"color_Adjust \{[^}]*\}", "color_Adjust { 0.6, 1.3, 4.5, 1 }", blue)
    spark = re.sub(r"color_Adjust \{[^}]*\}", "color_Adjust { 0.35, 0.7, 1.6, 1 }", blue)
    spark = spark.replace("fx/opt/fresnel\t\t1", "fx/opt/fresnel\t\t0")  # sprites face the camera: no rim falloff
    # gate B: bright violet, so the two ends can be told apart
    rim_b = re.sub(r"color_Adjust \{[^}]*\}", "color_Adjust { 3.2, 0.7, 4.5, 1 }", blue)
    spark_b = re.sub(r"color_Adjust \{[^}]*\}", "color_Adjust { 1.3, 0.35, 1.7, 1 }", spark)
    g, e = d.lookup("models/effects/materials/gameplay/powers/shadow_grapple/target/tr_sgrapple_target_05.material")[0]
    dark = d.read(g, e).decode("latin1").replace("\r\n", "\n")
    dark = re.sub(r"color_Adjust\s*\{[^}]*\}", "color_Adjust\t{ 0.01, 0.012, 0.04, 1 }", dark)
    # (use_softcontact 0 froze the game the first time a gate was drawn -- a material option combination missing from
    # the shader cache, compiled at run time; the gate entity sits higher above the surface instead)
    # (glow_m.tga as the mask made the quad invisible in game; Far Reach's own target mask stays)
    # the Void portal's distortion at gate size bends the whole view: a quarter of its strength
    g, e = d.lookup(f"{VOID}/vortex.material")[0]
    disto = d.read(g, e).decode("latin1").replace("\r\n", "\n")
    disto = re.sub(r"disto1_strength\s*\{[^}]*\}", "disto1_strength\t\t{ 0, 0, -0.005, -0.005 }", disto)
    for name, text in (("voidgate_rim", rim), ("voidgate_spark", spark), ("voidgate_void", dark),
                       ("voidgate_disto", disto), ("voidgate_rim_b", rim_b), ("voidgate_spark_b", spark_b)):
        assert "color_Adjust" in text
        write(f"generated/decls/material/{MAT}/{name}.material.decl", text)


def main():
    d = Deps()
    materials(d)
    g, e = d.lookup("models/effects/map_specific/void_house/rocks/portal/void_portal.particledt")[0]
    stock_particles = d.read(g, e).decode("latin1").replace("\r\n", "\n")
    g, e = d.lookup("models/effects/map_specific/void_house/rocks/portal/void_portal.fx")[0]
    stock_fx = d.read(g, e).decode("latin1").replace("\r\n", "\n")
    # gate A (blue), gate B (violet), and the light aim preview (rim + sparks only)
    for variant, color, name in (("gate", "", NAME), ("gate", "_b", FX + "/voidgate_gate_b"),
                                 ("preview", "", FX + "/voidgate_preview")):
        write(f"generated/decls/particledt/{name}.particledt.decl", make_particles(stock_particles, variant, color))
        fx = stock_fx.replace("models/effects/map_specific/void_house/rocks/portal/void_portal.particledt",
                              name + ".particledt")
        # the Void portal's light scatters into volumetric fog: in a street that is a white haze over everything
        fx = sub_block(fx, "m_scattering", "", 6)
        # stopping must end the particle system now (as Far Reach's aim marker does): the Void portal never sets it, and
        # every stopped gate / aim marker kept emitting forever
        assert fx.count("\t\t\t\t\t\t}\n\t\t\t\t\t\tm_timings = {") == 1
        fx = fx.replace("\t\t\t\t\t\t}\n\t\t\t\t\t\tm_timings = {",
                        "\t\t\t\t\t\t}\n\t\t\t\t\t\tm_waitSystemEnd = false;\n\t\t\t\t\t\tm_timings = {")
        # the Void portal opens over 1.5 s; a gate (and the aim preview) should be there at once
        fx = re.sub(r"(m_fadein = \{\s*m_max = )1\.5;(\s*m_min = )1\.5;", r"\g<1>0.15;\g<2>0.15;", fx)
        if variant == "preview":  # no light, no portal hum while aiming
            fx = sub_block(fx, "m_lights", "", 4)
            fx = sub_block(fx, "m_sounds", "", 4)
        write(f"generated/decls/fx/{name}.fx.decl", fx)
    # the gate entities: idEntity + cpntFX; the FX must die with the entity (it outlives it otherwise)
    for suffix, fxname in (("", NAME), ("_b", FX + "/voidgate_gate_b")):
        write(f"generated/decls/cpntfx/models/powers/voidgate/voidgate_gate{suffix}.cpntfx.decl",
              "{\n\tedit = {\n\t\tm_parameters = {\n"
              f"\t\t\tm_fx = \"{fxname}.fx\";\n"
              "\t\t\tm_updatePosition = true;\n\t\t\tm_stopOnParentRemoval = true;\n"
              "\t\t\tm_stopOnTargetRemoval = true;\n\t\t}\n\t}\n}\n")
        write(f"generated/decls/entitydef/models/powers/voidgate/voidgate_gate{suffix}.def.decl",
              "{\n\tclass = \"idEntity\";\n\tedit = {\n\t\tm_componentDecls = {\n\t\t\titem_begin = true;\n"
              "\t\t\titem_add[\"vgFx01\"] = {\n"
              f"\t\t\t\tinheritedDecl = \"models/powers/voidgate/voidgate_gate{suffix}.cpntfx\";\n"
              "\t\t\t}\n\t\t}\n\t}\n}\n")
    # passing through: a self-removing entity playing the Doppelganger decoy's blink-out (world-only; Blink's arrival
    # burst also flashed the player's screen edges when an NPC went through)
    write("generated/decls/cpntfx/models/powers/voidgate/voidgate_pass.cpntfx.decl",
          "{\n\tedit = {\n\t\tm_parameters = {\n"
          "\t\t\tm_fx = \"models/effects/gameplay/powers/doppelganger/decoy/blink/dganger_blink_out_01.fx\";\n"
          "\t\t}\n\t}\n}\n")  # (m_removeEntityOnFxEnd crashed: its end callback met a null entity; voidgate.dll
    #                   removes the flash entities itself a few seconds later)
    write("generated/decls/entitydef/models/powers/voidgate/voidgate_pass.def.decl",
          "{\n\tclass = \"idEntity\";\n\tedit = {\n\t\tm_componentDecls = {\n\t\t\titem_begin = true;\n"
          "\t\t\titem_add[\"vgFx02\"] = {\n"
          "\t\t\t\tinheritedDecl = \"models/powers/voidgate/voidgate_pass.cpntfx\";\n\t\t\t}\n\t\t}\n\t}\n}\n")
    # the Void portal's own meshes/materials/sounds exist only on the Void map: copy them into game1
    deps = ["models/effects/map_specific/void_house/rocks/portal/" + x for x in
            ("vortex.material", "vortex_blue.material", "vortex_color.material", "portal_vortex.lwo",
             "spawn_portal.lwo")]
    deps += ["models/effects/meshes/plane_1m_vfx.lwo"]
    deps += ["models/effects/meshes/torus_1m.lwo", "models/effects/map_specific/void_house/meshes/void_rock.lwo",
             "sound_events/bsp/bsp_fixint/bsp_fixint_void/void_portal_start",
             "sound_events/bsp/bsp_fixint/bsp_fixint_void/void_portal_end"]
    subprocess.check_call([sys.executable, os.path.join(os.path.dirname(__file__), "d2deps.py"), "copy", MOD] + deps)


if __name__ == "__main__":
    main()
