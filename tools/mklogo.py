#!/usr/bin/env python3
"""Regenerate CR_Monitor/Logo_APU.h from the APU artwork.

The sketch cannot decode a PNG at boot, so the mark is baked into flash as
a flat RGB565 array, already composited onto white. Run this only if the
source artwork or the splash size changes:

    python tools/mklogo.py path/to/apu-logo.png

Needs Pillow (pip install pillow).
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops

W, H = 260, 240          # splash size on the 480x320 panel
OUT = Path(__file__).resolve().parent.parent / "CR_Monitor" / "Logo_APU.h"


def content_bbox(rgb):
    """Bounding box of everything that is not the white background."""
    white = Image.new("RGB", rgb.size, (255, 255, 255))
    mask = ImageChops.difference(rgb, white).convert("L")
    return mask.point(lambda p: 255 if p > 12 else 0).getbbox()


def main(src):
    im = Image.open(src).convert("RGBA")
    flat = Image.new("RGB", im.size, (255, 255, 255))
    flat.paste(im, mask=im.getchannel("A"))

    # Trim the transparent margin first, otherwise the mark shrinks to sit
    # inside padding that is invisible anyway.
    flat = flat.crop(content_bbox(flat)).resize((W, H), Image.LANCZOS)

    px = flat.load()
    vals = [
        ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        for y in range(H)
        for r, g, b in [px[x, y] for x in range(W)]
    ]

    rows = "\n".join(
        "  " + ", ".join("0x%04X" % v for v in vals[i:i + 12]) + ","
        for i in range(0, len(vals), 12)
    )

    OUT.write_text(
        f"""/* APU logo - boot splash bitmap, generated from the university mark.
 *
 * {W} x {H} RGB565, composited onto white so it drops straight onto the
 * white splash background with no alpha blending at runtime. Regenerate
 * with tools/mklogo.py if the source artwork changes.
 *
 * {W * H * 2} bytes. const, so it lives in flash, not RAM.
 */

#pragma once

#define APU_LOGO_W {W}
#define APU_LOGO_H {H}

const uint16_t apuLogo[{W * H}] PROGMEM = {{
{rows}
}};
""",
        newline="\n",
    )
    print(f"wrote {OUT} ({W}x{H}, {W * H * 2} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
