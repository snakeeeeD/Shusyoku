# Draw simple game cursors (normal / hover / camera) and save as .cur (32/48/64) + preview.
# usage: python -I tools/cursors/make_cursors.py GameEngine/Assets/UI/Cursor
import sys, os, struct, math
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)
S = 256                     # draw size (supersampled), final sizes downscaled from this
FILL = (250, 240, 215, 255)     # cream
OUTL = (40, 22, 14, 255)        # dark brown
GOLD = (235, 190, 70, 255)
CYAN = (120, 220, 230, 255)


def outlined(draw_fn, ow):
    """draw shape twice: thick outline layer, then fill layer"""
    base = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    mask = Image.new("L", (S, S), 0)
    draw_fn(ImageDraw.Draw(mask), 255)
    # outline = dilated mask
    dil = mask.filter(ImageFilter.MaxFilter(ow * 2 + 1))
    base.paste(Image.new("RGBA", (S, S), OUTL), (0, 0), dil)
    return base, mask


def arrow(fill, accent):
    pts = [(14, 10), (14, 196), (62, 152), (96, 228), (128, 214), (96, 140), (160, 140)]
    base, mask = outlined(lambda d, c: d.polygon(pts, fill=c), 10)
    base.paste(Image.new("RGBA", (S, S), fill), (0, 0), mask)
    d = ImageDraw.Draw(base)
    d.line([(22, 30), (22, 170)], fill=accent, width=6)   # light edge
    return base, (14, 10)


def hand():
    def shape(d, c):
        d.rounded_rectangle((70, 8, 112, 170), radius=21, fill=c)      # index finger (long)
        d.rounded_rectangle((60, 126, 200, 244), radius=34, fill=c)    # palm + folded fingers
        d.rounded_rectangle((22, 140, 84, 186), radius=22, fill=c)     # thumb
    base, mask = outlined(shape, 10)
    base.paste(Image.new("RGBA", (S, S), FILL), (0, 0), mask)
    d = ImageDraw.Draw(base)
    for x in (112, 146, 178):                                          # finger gaps
        d.line([(x, 140), (x, 186)], fill=OUTL, width=7)
    d.rounded_rectangle((60, 222, 200, 244), radius=10, fill=GOLD)     # gold cuff
    return base, (91, 10)


def hand_press():
    """pointing hand while the button is down: index finger bent (short + knuckle), hand unchanged"""
    def shape(d, c):
        d.rounded_rectangle((70, 86, 112, 170), radius=21, fill=c)     # bent index finger (shorter)
        d.rounded_rectangle((60, 126, 200, 244), radius=34, fill=c)    # palm + folded fingers
        d.rounded_rectangle((22, 140, 84, 186), radius=22, fill=c)     # thumb
    base, mask = outlined(shape, 10)
    base.paste(Image.new("RGBA", (S, S), FILL), (0, 0), mask)
    d = ImageDraw.Draw(base)
    d.arc((72, 88, 110, 126), 200, 340, fill=OUTL, width=7)            # knuckle crease
    for x in (112, 146, 178):
        d.line([(x, 140), (x, 186)], fill=OUTL, width=7)
    d.rounded_rectangle((60, 222, 200, 244), radius=10, fill=GOLD)
    return base, (91, 10)                                              # same hotspot as hover: the hand stays, only the finger bends

def camera():
    c = S / 2

    def shape(d, col):
        L, W, H = 110, 26, 46        # arm length, shaft half width, head half width
        for ang in range(4):
            a = math.radians(ang * 90)
            ux, uy = math.cos(a), math.sin(a)
            px, py = -uy, ux
            def P(t, s): return (c + ux * t + px * s, c + uy * t + py * s)
            d.polygon([P(0, -W), P(L - 44, -W), P(L - 44, -H), P(L, 0), P(L - 44, H), P(L - 44, W), P(0, W)], fill=col)
        d.ellipse((c - 34, c - 34, c + 34, c + 34), fill=col)
    base, mask = outlined(shape, 10)
    base.paste(Image.new("RGBA", (S, S), CYAN), (0, 0), mask)
    d = ImageDraw.Draw(base)
    d.ellipse((c - 18, c - 18, c + 18, c + 18), fill=FILL, outline=OUTL, width=6)
    return base, (int(c), int(c))


def write_cur(path, img, hot):
    sizes = [32, 48, 64]
    imgs = []
    for sz in sizes:
        im = img.resize((sz, sz), Image.LANCZOS)
        w = h = sz
        px = im.load()
        xor = bytearray()
        for y in range(h - 1, -1, -1):                     # bottom-up BGRA
            for x in range(w):
                r, g, b, a = px[x, y]
                xor += bytes((b, g, r, a))
        row = ((w + 31) // 32) * 4
        andm = bytes(row * h)                               # all 0 -> alpha decides
        hdr = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(xor) + len(andm), 0, 0, 0, 0)
        hx, hy = round(hot[0] * sz / S), round(hot[1] * sz / S)
        imgs.append((sz, hx, hy, hdr + xor + andm))
    out = struct.pack("<HHH", 0, 2, len(imgs))
    off = 6 + 16 * len(imgs)
    data = b""
    for sz, hx, hy, blob in imgs:
        out += struct.pack("<BBBBHHII", sz % 256, sz % 256, 0, 0, hx, hy, len(blob), off)
        off += len(blob); data += blob
    open(path, "wb").write(out + data)


items = [("normal", *arrow(FILL, (255, 255, 255, 255))), ("hover", *hand()), ("hover_press", *hand_press()),
         ("camera", *camera())]
prev = Image.new("RGBA", (len(items) * 160 + 40, 200), (34, 24, 44, 255))
for i, (name, img, hot) in enumerate(items):
    write_cur(os.path.join(OUT, name + ".cur"), img, hot)
    big = img.resize((96, 96), Image.LANCZOS); small = img.resize((32, 32), Image.LANCZOS)
    prev.alpha_composite(big, (20 + i * 160, 20))
    prev.alpha_composite(small, (20 + i * 160 + 110, 84))
prev.save(os.path.join(os.path.dirname(os.path.abspath(__file__)), "preview.png"))   # preview next to this script
print("ok")
