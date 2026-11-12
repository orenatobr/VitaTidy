#!/usr/bin/env python3
"""Generates the VPK artwork (icon, LiveArea background and gate image). Needs Pillow."""
import os
from PIL import Image, ImageDraw, ImageFont

BG, TILE, TILE2, GREEN, WHITE = (15, 23, 42), (30, 41, 59), (51, 65, 85), (34, 197, 94), (248, 250, 252)
OUT = os.path.join(os.path.dirname(__file__), "..", "sce_sys")


def font(size):
    for path in ("/System/Library/Fonts/Supplemental/Arial Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"):
        if os.path.exists(path):
            return ImageFont.truetype(path, size)
    return ImageFont.load_default()


def grid(d, x, y, cell, gap, radius, check_at=(2, 0)):
    """3x3 tiles; one tile is green with a check, the 'completed' game."""
    for r in range(3):
        for c in range(3):
            x0, y0 = x + c * (cell + gap), y + r * (cell + gap)
            done = (c, r) == check_at
            d.rounded_rectangle((x0, y0, x0 + cell, y0 + cell), radius, fill=GREEN if done else (TILE2 if (r + c) % 2 else TILE))
            if done:
                s = cell
                pts = [(x0 + s * .24, y0 + s * .54), (x0 + s * .43, y0 + s * .72), (x0 + s * .78, y0 + s * .30)]
                d.line(pts, fill=WHITE, width=max(2, int(s * .12)), joint="curve")


def save8(im, path):
    """The Vita installer rejects PNGs in sce_sys unless they are exactly 8-bit indexed (error 0x8010113D).

    24-bit is rejected, and so is a smaller bit depth, which PIL picks on its own when optimize is on.
    """
    im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, bits=8, optimize=False)


def icon():
    im = Image.new("RGB", (128, 128), BG)
    grid(ImageDraw.Draw(im), 14, 14, 30, 7, 6)
    save8(im, os.path.join(OUT, "icon0.png"))


def background():
    im = Image.new("RGB", (840, 500), BG)
    d = ImageDraw.Draw(im)
    grid(d, 470, 70, 110, 18, 18)
    d.text((60, 190), "VitaTidy", font=font(72), fill=WHITE)
    d.text((64, 285), "Reorder the home screen.\nMark finished games.", font=font(26), fill=(148, 163, 184), spacing=10)
    save8(im, os.path.join(OUT, "livearea", "contents", "bg.png"))


def startup():
    im = Image.new("RGB", (280, 158), BG)
    d = ImageDraw.Draw(im)
    grid(d, 24, 28, 28, 6, 5)
    d.text((140, 56), "VitaTidy", font=font(30), fill=WHITE)
    save8(im, os.path.join(OUT, "livearea", "contents", "startup.png"))


if __name__ == "__main__":
    os.makedirs(os.path.join(OUT, "livearea", "contents"), exist_ok=True)
    icon(); background(); startup()
