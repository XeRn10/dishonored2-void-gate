"""Builds the Void Gate release: release/VoidGateSetup.exe and release/VoidGate-<version>.zip.

  1. native_d2\\build.cmd (dinput8.dll + voidgate.dll), unless --no-native
  2. release/payload: the data mod (mod.json, files/ without the stock copies -- the setup takes those from the
     player's game --, lang/), bin/*.dll, sig.json (the code checks of native_d2/vg_sig.h). This is what
     tools/vg_setup.py, the Python reference setup, installs (tests compare the native setup with it).
  3. the native setup: tools/vg_payload.py packs the same things for C (native_d2/setup/gen), then
     native_d2\\setup\\build.cmd test builds native_d2/setup/VoidGateSetup.exe (and test_cli.exe)
  4. the zip for Nexus / GitHub releases: the native exe + README.txt

  python tools\\vg_build.py [--no-native] [--no-exe]
"""
import json
import os
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
MOD = os.path.join(ROOT, "d2mods", "VoidGate")
NATIVE = os.path.join(ROOT, "native_d2")
OUT = os.path.join(ROOT, "release")
PAYLOAD = os.path.join(OUT, "payload")

sys.path.insert(0, os.path.join(ROOT, "tools"))
from vg_setup import VERSION  # noqa: E402
import vg_payload  # noqa: E402


def sig_json():
    text = open(os.path.join(NATIVE, "vg_sig.h"), encoding="utf-8").read()
    checks = [[int(m.group(1), 16), bytes(int(x, 16) for x in m.group(2).split(",")).hex()]
              for m in re.finditer(r"\{0x([0-9A-Fa-f]+)ull, \{([^}]*)\}\}", text)]
    ts = int(re.search(r"VG_SIG_TIMESTAMP 0x([0-9A-Fa-f]+)u", text).group(1), 16)
    return {"timestamp": ts, "checks": checks}


def payload():
    shutil.rmtree(PAYLOAD, ignore_errors=True)
    dst = os.path.join(PAYLOAD, "VoidGate")
    info = json.load(open(os.path.join(MOD, "mod.json"), encoding="utf-8"))
    copies = {p for p, e in info.get("entries", {}).items() if e.get("copy")}
    root = os.path.join(MOD, "files")
    n = skipped = 0
    for dp, _, fns in os.walk(root):
        for fn in fns:
            rel = os.path.relpath(os.path.join(dp, fn), root).replace(os.sep, "/")
            if rel in copies:
                skipped += 1
                continue
            os.makedirs(os.path.join(dst, "files", os.path.dirname(rel)), exist_ok=True)
            shutil.copy2(os.path.join(dp, fn), os.path.join(dst, "files", rel))
            n += 1
    shutil.copytree(os.path.join(MOD, "lang"), os.path.join(dst, "lang"))
    shutil.copy2(os.path.join(MOD, "mod.json"), dst)
    os.makedirs(os.path.join(PAYLOAD, "bin"))
    for fn in ("dinput8.dll", "voidgate.dll"):
        shutil.copy2(os.path.join(NATIVE, fn), os.path.join(PAYLOAD, "bin", fn))
    sig = sig_json()
    json.dump(sig, open(os.path.join(PAYLOAD, "sig.json"), "w"))
    print(f"payload: {n} mod files (+{skipped} stock copies taken from the player's game), "
          f"{len(sig['checks'])} code checks")


def main():
    os.makedirs(OUT, exist_ok=True)
    if "--no-native" not in sys.argv:
        subprocess.run(["cmd", "/c", os.path.join(NATIVE, "build.cmd")], check=True)
    payload()
    if "--no-exe" in sys.argv:
        return
    vg_payload.main(VERSION)
    subprocess.run(["cmd", "/c", os.path.join(NATIVE, "setup", "build.cmd"), "test"], check=True)
    exe = os.path.join(OUT, "VoidGateSetup.exe")
    shutil.copy2(os.path.join(NATIVE, "setup", "VoidGateSetup.exe"), exe)
    z = os.path.join(OUT, f"VoidGate-{VERSION}.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as f:
        f.write(exe, "VoidGateSetup.exe")
        readme = os.path.join(MOD, "README.txt")
        if os.path.isfile(readme):
            f.write(readme, "README.txt")
    print(f"{exe}  {os.path.getsize(exe) // 1024} KB\n{z}  {os.path.getsize(z) // 1024} KB")


if __name__ == "__main__":
    main()
