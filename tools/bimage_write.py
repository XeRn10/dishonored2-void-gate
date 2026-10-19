"""Write Void Engine .bimage7 (BC3, single mip) from a PIL image, using a stock image of the same size as header
template (only width/height/data size fields are rewritten). See bimage.py for the layout.

  encode(img_rgba, template_bytes) -> bytes
"""
import struct

import etcpak
import numpy as np
from PIL import Image

import bimage


def encode(img, template):
    img = img.convert("RGBA")
    w, h = img.size
    assert w % 4 == 0 and h % 4 == 0, "BC3 needs sizes divisible by 4"
    assert template[:3] == b"BIM" and struct.unpack_from("<I", template, 0x10)[0] == 5, "template must be BC3"
    data = etcpak.compress_bc3(np.asarray(img).tobytes(), w, h)
    hdr = bytearray(template[:0x52])
    struct.pack_into("<II", hdr, 0x18, w, h)
    struct.pack_into("<II", hdr, 0x20, w, h)
    struct.pack_into("<IIII", hdr, 0x42, 0, w, h, len(data))
    out = bytes(hdr) + data
    # round trip: decode and compare with the source (BC3 is lossy; check the mean error)
    back = np.asarray(bimage.decode(out)).astype(int)
    err = np.abs(back - np.asarray(img).astype(int)).mean()
    if err > 6:
        raise ValueError(f"BC3 round trip error too high: {err:.2f}")
    return out


if __name__ == "__main__":
    import sys
    src, tpl, dst = sys.argv[1:4]
    open(dst, "wb").write(encode(Image.open(src), open(tpl, "rb").read()))
