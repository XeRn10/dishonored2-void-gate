"""Void Gate see-through window: the data side (docs/d2_portal_render_research.md, section 5.4).

The only shader that shows the engine's second view (the Timepiece lens, `rendering/timeTravelPortal`) samples it in
screen space, and only `portal.material` uses it -- with `useSkinning 1`, so it needs a skinned mesh (a missing shader
permutation freezes the renderer). The one flat skinned plane in the game is Delilah's portal in the Dunwall tower
(`models/effects/map_specific/dunwall_return_tower/portal.md6`, 5.87 x 3.63 m, in the YZ plane, bottom at the origin).
This writes:
  * a copy of portal.material showing the second view 1:1 (no refraction, no blur, black glass mask), named so that its
    path has exactly the length of the plane's material reference;
  * a copy of the plane's compiled model with that material reference swapped (same length: nothing else moves);
  * an md6 decl and an entityDef for it (`models/powers/voidgate/voidgate_view.def`);
  * the plane's stock skeleton / anims copied into game1 (d2deps), plus the mod.json entry for the new model.

  py make_voidgate_view.py
"""
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from d2mod import Stock  # noqa: E402

MOD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "d2mods", "VoidGate")
STOCK_MODEL = "generated/basemodel/models/effects/map_specific/dunwall_return_tower/portal.bmd6model"
STOCK_MAT_REF = b"models/effects/map_specific/dunwall_return/tower/painting/materials/painting_temp_01.material"
VIEW = "models/powers/voidgate/voidgate_view"
MAT_DIR = "models/effects/materials/gameplay/powers/voidgate/"


def material_name():
    base, ext = MAT_DIR + "voidgate_view", ".material"
    pad = len(STOCK_MAT_REF) - len(base) - len(ext)
    assert pad >= 1, pad
    return base + "_" * pad + ext


HALF_Y, HALF_Z = 2.936, 3.635 / 2  # the plane: y -2.936..2.936, z 0..3.635 (x is its 4 mm thickness)


def oval(model):
    """The plane's vertex positions (float32 x, y, z, packed from offset 282) pulled from the rectangle onto the
    ellipse inscribed in it (square-to-disc map), so the window is an oval like the gate (scaled in game by the
    entity's axis to 1.7 x 2.8 m)."""
    import math
    import struct
    buf = bytearray(model)
    off, n = 282, 0
    while off + 12 <= len(buf):
        x, y, z = struct.unpack_from("<fff", buf, off)
        if not (-0.011 <= x <= 0.011 and abs(y) <= HALF_Y + 1e-3 and -0.011 <= z <= 2 * HALF_Z + 1e-3):
            break
        u = max(-1.0, min(1.0, y / HALF_Y))
        v = max(-1.0, min(1.0, (z - HALF_Z) / HALF_Z))
        u2, v2 = u * math.sqrt(1 - v * v / 2), v * math.sqrt(1 - u * u / 2)
        struct.pack_into("<fff", buf, off, x, u2 * HALF_Y, v2 * HALF_Z + HALF_Z)
        off, n = off + 12, n + 1
    print("oval: moved", n, "vertices")
    assert n > 100, n
    return bytes(buf)


def write(rel, data):
    p = os.path.join(MOD, "files", rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))
    print("wrote", rel, len(data))


def main():
    st = Stock()
    mat = material_name()
    assert len(mat) == len(STOCK_MAT_REF)
    # 1. the material: portal.material with the second view shown as is (only parms and textures changed: the
    #    options select the shader permutation, and those must stay the stock lens' ones)
    src = st.read(1, "generated/decls/material/models/effects/materials/gameplay/powers/timetravel/portal.material.decl")
    text = src.decode("utf-8")
    for old, new in [("tsd/RefractionStrength\t{ 15, 25 }", "tsd/RefractionStrength\t{ 0, 0 }"),
                     ("tsd/BlurStrength\t\t22", "tsd/BlurStrength\t\t0"),
                     ("tsd/failurerefractionstrength\t\t{ 20, 45 }", "tsd/failurerefractionstrength\t\t{ 0, 0 }"),
                     ("tsd/FailureBlurStrength\t\t\t5", "tsd/FailureBlurStrength\t\t\t0"),
                     # the glass mask (x = 1: lit lens surface, 0: the second view): all view
                     ("models/weapons/timeshift_device/timeshift_device_portal_portalmask.tga",
                      "models/effects/textures/test/black.tga"),
                     # portalFactor = max(failure noise, 1 - tsd/FailFactor), and FailFactor is set only by the
                     # Timepiece's code: unset it read as 1, so the view showed only where the noise map is white
                     # (a shard pattern). An all-white noise map: the whole view, whatever FailFactor is.
                     ("models/weapons/timeshift_device/timeshift_device_portal_failnoise.tga",
                      "models/effects/textures/test/white.tga")]:
        assert old in text, old
        text = text.replace(old, new)
    write(f"generated/decls/material/{mat[:-len('.material')]}.material.decl", text)
    # 2. the plane's compiled model, its material reference swapped in place
    model = st.read(2, STOCK_MODEL)
    assert model.count(STOCK_MAT_REF) == 2  # (both length-prefixed: mesh surface and material list)
    write(f"generated/basemodel/{VIEW}.bmd6model", oval(model.replace(STOCK_MAT_REF, mat.encode())))
    # (debug variant: the stock rectangle, only the material swapped)
    write(f"generated/basemodel/{VIEW}_rect.bmd6model", model.replace(STOCK_MAT_REF, mat.encode()))
    # 3. md6 decl (stock skeleton and anims) and the entity
    md6 = st.read(2, "generated/decls/md6def/models/effects/map_specific/dunwall_return_tower/portal.md6.decl").decode()
    assert '"models/effects/map_specific/dunwall_return_tower/portal.md6mesh"' in md6
    md6 = md6.replace('"models/effects/map_specific/dunwall_return_tower/portal.md6mesh"', f'"{VIEW}.md6mesh"')
    write(f"generated/decls/md6def/{VIEW}.md6.decl", md6)
    ent = st.read(2, "generated/decls/entitydef/models/effects/map_specific/dunwall_return_tower/portal.def.decl").decode()
    ent = ent.replace("models/effects/map_specific/dunwall_return_tower/portal.md6", f"{VIEW}.md6")
    write(f"generated/decls/entitydef/{VIEW}.def.decl", ent)
    write(f"generated/decls/md6def/{VIEW}_rect.md6.decl", md6.replace(f'"{VIEW}.md6mesh"', f'"{VIEW}_rect.md6mesh"'))
    write(f"generated/decls/entitydef/{VIEW}_rect.def.decl", ent.replace(f"{VIEW}.md6", f"{VIEW}_rect.md6"))
    # 4. mod.json: the new model's index entry (type baseModel, named like the md6 decl's mesh)
    mj_path = os.path.join(MOD, "mod.json")
    mj = json.load(open(mj_path, encoding="utf-8"))
    mj.setdefault("entries", {})[f"generated/basemodel/{VIEW}.bmd6model"] = {
        "type": "baseModel", "name": f"{VIEW}.md6mesh", "tail": "00000000000000000000"}
    mj["entries"][f"generated/basemodel/{VIEW}_rect.bmd6model"] = {
        "type": "baseModel", "name": f"{VIEW}_rect.md6mesh", "tail": "00000000000000000000"}
    json.dump(mj, open(mj_path, "w", encoding="utf-8", newline="\n"), indent=2)
    print("material:", mat)
    # 5. stock dependencies not in game1 (skeleton, anims, animset/animbasic, the stock model itself)
    subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "d2deps.py"), "copy",
                    MOD, "models/effects/map_specific/dunwall_return_tower/portal.def"], check=True)


if __name__ == "__main__":
    main()
