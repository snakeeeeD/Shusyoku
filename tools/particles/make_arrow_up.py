# White up-arrow particle for buff effects (tinted by colorStart/colorEnd in effects.json).
# usage: python -I tools/particles/make_arrow_up.py GameEngine/Assets/Particles/arrow_up.png
import sys
from PIL import Image, ImageDraw, ImageFilter

OUT = sys.argv[1]
S = 256
mask = Image.new("L", (S, S), 0)
d = ImageDraw.Draw(mask)
d.polygon([(128, 18), (230, 128), (170, 128), (170, 236), (86, 236), (86, 128), (26, 128)], fill=255)   # arrow
glow = mask.filter(ImageFilter.GaussianBlur(14)).point(lambda v: v * 0.55)                              # soft halo
alpha = Image.composite(mask, glow, mask)          # solid arrow over its halo
img = Image.new("RGBA", (S, S), (255, 255, 255, 0))
img.putalpha(alpha)
img.resize((64, 64), Image.LANCZOS).save(OUT)
print("ok", OUT)
