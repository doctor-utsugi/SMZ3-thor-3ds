"""Writes the SMZ3 launcher icon (legacy mipmaps, round variants, adaptive foreground and monochrome layers) into the
vendored Lemuroid resources from assets/logo.png. Run: python3 tools/gen_app_icon.py"""
import os
from PIL import Image, ImageDraw, ImageChops

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "vendor/Lemuroid/lemuroid-app/src/main/res")
LOGO = Image.open(os.path.join(ROOT, "assets/logo.png")).convert("RGB")
BG = LOGO.getpixel((4, 4))                      # the black around the rounded square
BOX = (90, 90, 934, 934)                        # the rounded square and a little margin
SIZES = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def legacy(size, round_):
    img = LOGO.crop(BOX).resize((size, size), Image.LANCZOS).convert("RGBA")
    mask = Image.new("L", (size * 4, size * 4), 0)
    d = ImageDraw.Draw(mask)
    if round_:
        d.ellipse((0, 0, size * 4 - 1, size * 4 - 1), fill=255)
    else:
        d.rounded_rectangle((0, 0, size * 4 - 1, size * 4 - 1), radius=size * 4 // 5, fill=255)
    img.putalpha(mask.resize((size, size), Image.LANCZOS))
    return img


def layer(size, scale=0.66):
    """The logo on a transparent square, its picture kept inside the adaptive icon's safe zone."""
    inner = int(size * scale)
    pic = LOGO.crop(BOX).resize((inner, inner), Image.LANCZOS).convert("RGBA")
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.paste(pic, ((size - inner) // 2, (size - inner) // 2))
    return out


def monochrome(size):
    """A white silhouette of what is bright in the logo (the shell, the triangle, the fangs' edges)."""
    pic = layer(size)
    grey = ImageChops.multiply(pic.convert("L"), pic.getchannel("A"))
    alpha = grey.point(lambda v: 255 if v > 125 else 0)
    out = Image.new("RGBA", (size, size), (255, 255, 255, 0))
    out.putalpha(alpha)
    return out


for name, size in SIZES.items():
    d = os.path.join(RES, f"mipmap-{name}")
    os.makedirs(d, exist_ok=True)
    legacy(size, False).save(os.path.join(d, "lemuroid_launcher.png"))
    legacy(size, True).save(os.path.join(d, "lemuroid_launcher_round.png"))

nodpi = os.path.join(RES, "drawable-nodpi")
layer(432).save(os.path.join(nodpi, "smz3_launcher_foreground.png"))
monochrome(432).save(os.path.join(nodpi, "smz3_launcher_monochrome.png"))
with open(os.path.join(RES, "values/lemuroid_launcher_background.xml"), "w") as f:
    f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="lemuroid_launcher_background">#%02x%02x%02x</color>\n</resources>\n' % BG)
