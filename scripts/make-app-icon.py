"""Package the supplied DMIND PNG as a multi-resolution Windows icon.

Requires Pillow. The checked-in ICO is used by normal builds.
"""
from pathlib import Path

from PIL import Image


def main():
    assets = Path(__file__).resolve().parents[1] / "assets"
    with Image.open(assets / "dmind.png") as source:
        if source.size != (1200, 1200):
            raise ValueError("Expected the supplied 1200 x 1200 logo.")
        image = source.convert("RGBA")
        image.save(
            assets / "dmind.ico",
            format="ICO",
            bitmap_format="bmp",
            sizes=[(size, size) for size in (16, 20, 24, 32, 40, 48, 64, 128, 256)],
        )


if __name__ == "__main__":
    main()
