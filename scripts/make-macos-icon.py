#!/usr/bin/env python3
"""Build the macOS app icon (images/jpegsnoop.icns) from images/jpegsnoop.ico.

Usage: uv run --no-project --with pillow python -I scripts/make-macos-icon.py
   or: python3 scripts/make-macos-icon.py   (with Pillow installed)

The Windows icon is pixel art at 16, 32 and 48 pixels. macOS wants sizes up to
1024, so the larger sizes are the 48-pixel art enlarged by a whole number
(each pixel a sharp square, no blur) and centred at 75% of the canvas, close to
the margin Apple's icon grid leaves. 16 and 32 use the hand-drawn frames as
they are, and 64 is the 32-pixel frame doubled. Needs macOS (iconutil).

The .icns is checked in, so building the app doesn't need Pillow; run this
again only if the .ico changes.
"""

import pathlib
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

repo = pathlib.Path(__file__).resolve().parent.parent
src = repo / "images" / "jpegsnoop.ico"
dst = repo / "images" / "jpegsnoop.icns"


def frame(ico, size):
    return ico.ico.getimage((size, size)).convert("RGBA")


def enlarged(art, canvas):
    """art enlarged by the whole number that fills about 75% of canvas, centred."""
    factor = max(1, round(canvas * 0.75 / art.width))
    big = art.resize((art.width * factor, art.height * factor), Image.NEAREST)
    out = Image.new("RGBA", (canvas, canvas), (0, 0, 0, 0))
    out.paste(big, ((canvas - big.width) // 2, (canvas - big.height) // 2))
    return out


def main():
    if shutil.which("iconutil") is None:
        sys.exit("error: iconutil not found (it comes with macOS)")

    ico = Image.open(src)
    art16, art32, art48 = frame(ico, 16), frame(ico, 32), frame(ico, 48)

    # name in the .iconset -> image
    images = {
        "icon_16x16.png": art16,
        "icon_16x16@2x.png": art32,
        "icon_32x32.png": art32,
        "icon_32x32@2x.png": art32.resize((64, 64), Image.NEAREST),
    }
    for size in (128, 256, 512):
        images[f"icon_{size}x{size}.png"] = enlarged(art48, size)
        images[f"icon_{size}x{size}@2x.png"] = enlarged(art48, size * 2)

    with tempfile.TemporaryDirectory() as tmp:
        iconset = pathlib.Path(tmp) / "jpegsnoop.iconset"
        iconset.mkdir()
        for name, img in images.items():
            img.save(iconset / name)
        subprocess.run(
            ["iconutil", "--convert", "icns", "--output", str(dst), str(iconset)],
            check=True,
        )

    print(f"wrote {dst.relative_to(repo)}")


if __name__ == "__main__":
    main()
