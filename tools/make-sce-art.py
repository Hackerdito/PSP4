#!/usr/bin/env python3
"""Generates sce_sys/pic1.png (1920x1080), the background the PS4 menu shows behind SAMPSP4.

The app icon (sce_sys/icon0.png) is the owner's artwork (source: sce_sys/icon-source.png)."""
import os
from PIL import Image, ImageDraw, ImageFilter

BG = (7, 8, 12)
BLUE = (0, 66, 252)        # SAMPSP4 blue, from the icon
SKY = (63, 123, 255)


def pic1():
    W, H = 1920, 1080
    layer = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(layer)
    for (cx, cy, r, col) in [(W * 0.12, H * 0.95, 560, BLUE), (W * 0.84, H * 0.22, 620, SKY)]:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
    layer = layer.filter(ImageFilter.GaussianBlur(220))
    return Image.blend(Image.new('RGB', (W, H), BG), layer, 0.55)


if __name__ == '__main__':
    out = os.path.join(os.path.dirname(__file__), '..', 'sce_sys')
    pic1().save(os.path.join(out, 'pic1.png'))
    print('ok')
