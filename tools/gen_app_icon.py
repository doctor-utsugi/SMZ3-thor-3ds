"""Draws the SMZ3 launcher icon (legacy mipmaps, round variants, adaptive foreground and monochrome layers) into the
vendored Lemuroid resources. Run: python3 tools/gen_app_icon.py"""
import os
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "vendor/Lemuroid/lemuroid-app/src/main/res")
FONT = "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf"
GOLD, GOLD_DARK, BG_TOP, BG_BOTTOM = (240, 190, 70), (190, 130, 30), (26, 36, 68), (10, 14, 28)


def gradient(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    for y in range(size):
        t = y / (size - 1)
        c = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
        for x in range(size):
            px[x, y] = c
    return img


def emblem(size, color=GOLD, shade=GOLD_DARK):
    """The triangle and the lettering on a transparent square, kept inside the central 62% (adaptive icon safe zone)."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    s = size
    apex, left, right = (s * 0.5, s * 0.22), (s * 0.25, s * 0.60), (s * 0.75, s * 0.60)
    d.polygon([apex, left, right], fill=color)
    inner = [(s * 0.5, s * 0.34), (s * 0.4, s * 0.52), (s * 0.6, s * 0.52)]
    d.polygon(inner, fill=(0, 0, 0, 0))
    d.line([apex, left, right, apex], fill=shade, width=max(1, s // 60), joint="curve")
    font = ImageFont.truetype(FONT, int(s * 0.17))
    text = "SMZ3"
    w = d.textlength(text, font=font)
    d.text(((s - w) / 2, s * 0.64), text, font=font, fill=color)
    return img


def legacy(size, round_):
    big = 1024
    base = gradient(big).convert("RGBA")
    base.alpha_composite(emblem(big))
    mask = Image.new("L", (big, big), 0)
    md = ImageDraw.Draw(mask)
    if round_:
        md.ellipse((0, 0, big - 1, big - 1), fill=255)
    else:
        md.rounded_rectangle((0, 0, big - 1, big - 1), radius=big // 5, fill=255)
    base.putalpha(mask)
    return base.resize((size, size), Image.LANCZOS)


def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path)


for density, px in (("mdpi", 48), ("hdpi", 72), ("xhdpi", 96), ("xxhdpi", 144), ("xxxhdpi", 192)):
    save(legacy(px, False), f"{RES}/mipmap-{density}/lemuroid_launcher.png")
    save(legacy(px, True), f"{RES}/mipmap-{density}/lemuroid_launcher_round.png")

fg = emblem(432)
save(fg, f"{RES}/drawable-nodpi/smz3_launcher_foreground.png")
mono = emblem(432, color=(255, 255, 255, 255), shade=(255, 255, 255, 255))
save(mono, f"{RES}/drawable-nodpi/smz3_launcher_monochrome.png")
print("icons written")
