# Mouse guide icons for the camera demo: right-drag / wheel / wheel-click.
# usage: python -I tools/cursors/make_mouse_icons.py GameEngine/Assets/UI/Cursor
import sys, os
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 512
FILL = (250, 240, 215, 255)
OUTL = (40, 22, 14, 255)
GOLD = (245, 195, 60, 255)
DIM = (205, 195, 175, 255)


def base_mouse(hl):
    """hl: 'right' | 'wheel'"""
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    bx0, by0, bx1, by1 = 136, 60, 376, 452            # body
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle((bx0, by0, bx1, by1), radius=118, fill=255)
    img.paste(Image.new("RGBA", (S, S), OUTL), (0, 0), mask.filter(ImageFilter.MaxFilter(25)))
    img.paste(Image.new("RGBA", (S, S), FILL), (0, 0), mask)
    d = ImageDraw.Draw(img)
    midx, split = (bx0 + bx1) // 2, 230
    if hl == "right":                                   # light the right button (clip by body mask)
        btn = Image.new("L", (S, S), 0)
        ImageDraw.Draw(btn).rectangle((midx, by0, bx1, split), fill=255)
        btn = Image.composite(btn, Image.new("L", (S, S), 0), mask)
        img.paste(Image.new("RGBA", (S, S), GOLD), (0, 0), btn)
    d.line([(bx0, split), (bx1, split)], fill=OUTL, width=12)          # buttons / body
    d.line([(midx, by0), (midx, split)], fill=OUTL, width=12)          # left / right
    wc = GOLD if hl == "wheel" else DIM
    d.rounded_rectangle((midx - 26, 104, midx + 26, 196), radius=24, fill=wc, outline=OUTL, width=10)
    return img, d


def arrow(d, x, y, dx, dy, col=GOLD):
    s = 50
    if dy:
        pts = [(x, y + dy * s), (x - s, y), (x + s, y)]
    else:
        pts = [(x + dx * s, y), (x, y - s), (x, y + s)]
    d.polygon(pts, fill=col, outline=OUTL, width=8)


def rdrag():
    img, d = base_mouse("right")
    # small 4-way hint at lower right
    cx, cy = 420, 392
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        arrow(d, cx + dx * 50, cy + dy * 50, dx, dy)
    return img


def wheel():
    img, d = base_mouse("wheel")
    arrow(d, 440, 110, 0, -1)   # up / down beside the wheel
    arrow(d, 440, 200, 0, 1)
    return img


def mclick():
    img, d = base_mouse("wheel")
    for k in range(3):                                   # click burst above the wheel
        ang = [(-1, -1), (0, -1.3), (1, -1)][k]
        x0, y0 = 256 + ang[0] * 60, 100 + ang[1] * 40
        d.line([(x0, y0), (x0 + ang[0] * 40, y0 + ang[1] * 40)], fill=GOLD, width=22)
    return img


prev = Image.new("RGBA", (3 * 140 + 20, 160), (34, 24, 44, 255))
for i, (name, fn) in enumerate((("mouse_rdrag", rdrag), ("mouse_wheel", wheel), ("mouse_mclick", mclick))):
    im = fn().resize((128, 128), Image.LANCZOS)
    im.save(os.path.join(OUT, name + ".png"))
    prev.alpha_composite(im, (20 + i * 140, 16))
prev.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview_mouse.png"))
print("ok")
