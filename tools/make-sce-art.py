#!/usr/bin/env python3
"""Generates sce_sys/icon0.png (512x512) and sce_sys/pic1.png (1920x1080) for the VAULT package."""
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

BG, ACCENT, TEXT = (7, 8, 12), (61, 245, 200), (242, 244, 248)
FONT = os.path.join(os.path.dirname(__file__), '..', 'assets', 'fonts', 'SpaceGrotesk-Bold.ttf')


def glow(size, spots):
    w, h = size
    img = Image.new('RGB', size, BG)
    layer = Image.new('RGB', size, BG)
    d = ImageDraw.Draw(layer)
    for (cx, cy, r, col) in spots:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
    layer = layer.filter(ImageFilter.GaussianBlur(min(w, h) / 5))
    return Image.blend(img, layer, 0.55)


def mark(d, cx, cy, s, width):
    # The VAULT "V" with a dot, same as the in-app logo.
    d.line([(cx - s, cy - s * 0.85), (cx, cy + s * 0.85), (cx + s, cy - s * 0.85)], fill=ACCENT, width=width, joint='curve')
    r = width * 0.75
    d.ellipse([cx - r, cy - s * 0.55 - r, cx + r, cy - s * 0.55 + r], fill=ACCENT)


def icon():
    S = 2048  # supersample
    img = glow((S, S), [(S * 0.2, S * 0.95, S * 0.5, (27, 108, 255)), (S * 0.85, S * 0.15, S * 0.45, ACCENT)])
    d = ImageDraw.Draw(img)
    mark(d, S / 2, S * 0.40, S * 0.2, int(S * 0.05))
    f = ImageFont.truetype(FONT, int(S * 0.13))
    text = 'VAULT'
    spacing = S * 0.03
    widths = [d.textlength(ch, font=f) for ch in text]
    total = sum(widths) + spacing * (len(text) - 1)
    x = (S - total) / 2
    for ch, w in zip(text, widths):
        d.text((x, S * 0.68), ch, font=f, fill=TEXT)
        x += w + spacing
    return img.resize((512, 512), Image.LANCZOS)


def pic1():
    W, H = 1920, 1080
    return glow((W, H), [(W * 0.1, H * 0.95, 520, (27, 108, 255)), (W * 0.82, H * 0.25, 600, ACCENT)])


if __name__ == '__main__':
    out = os.path.join(os.path.dirname(__file__), '..', 'sce_sys')
    icon().save(os.path.join(out, 'icon0.png'))
    pic1().save(os.path.join(out, 'pic1.png'))
    print('ok')
