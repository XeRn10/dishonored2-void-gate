"""Decoder for Void Engine .bimage7 textures (first mip only).

Layout: "BIM" + version byte, then u32 fields; width/height at 0x18/0x1C,
format code at 0x10, mip count at 0x14. First mip header at 0x42:
u32 mip index, u32 width, u32 height, u32 data size; data starts at 0x52.
"""
import struct

import texture2ddecoder as t2d
from PIL import Image

# format code -> (decoder, bytes per 4x4 block); None decoder = raw RGBA8
FORMATS = {
    5: (t2d.decode_bc3, 16),
}


def decode(data):
    assert data[:3] == b"BIM", "not a bimage"
    fmt, = struct.unpack_from("<I", data, 0x10)
    vw, vh = struct.unpack_from("<II", data, 0x18)  # visible size, may be < mip size
    _, w, h, size = struct.unpack_from("<IIII", data, 0x42)
    payload = data[0x52:0x52 + size]
    pw, ph = (w + 3) // 4 * 4, (h + 3) // 4 * 4
    blocks = (pw // 4) * (ph // 4)
    if fmt in FORMATS:
        fn, bpb = FORMATS[fmt]
    elif size == blocks * 8:
        fn, bpb = t2d.decode_bc1, 8
    elif size == w * h * 4:
        return Image.frombytes("RGBA", (w, h), payload)
    else:
        raise ValueError(f"unknown bimage format {fmt} ({w}x{h}, {size} bytes)")
    if size != blocks * bpb:
        raise ValueError(f"size mismatch for format {fmt}: {size} != {blocks * bpb}")
    px = fn(payload, pw, ph)
    return Image.frombytes("RGBA", (pw, ph), px, "raw", "BGRA").crop((0, 0, min(w, vw), min(h, vh)))
