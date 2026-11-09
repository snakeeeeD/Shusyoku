# 追加のポーズ絵（AI）を、作業枠（1536・足元1227・中心768）に、正面の絵と頭の大きさをそろえて置く
import sys, os
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
ns = {"__file__": os.path.join(HERE, "prep.py")}
exec(open(os.path.join(HERE, "prep.py"), encoding="utf-8").read().split("a1 = remove_bg")[0], ns)

def head_h(f):
    al = f[..., 3] > 0; r, g, b = f[..., 0].astype(int), f[..., 1].astype(int), f[..., 2].astype(int)
    red = al & (r > 140) & (g < 70) & (b < 80)
    hair = al & (r > 60) & (r < 160) & (g > 35) & (g < 110) & (b < 100) & (r > g + 10)
    return np.where(red.any(1))[0][0] - np.where(hair.any(1))[0][0]

def place_pose(src, ref, out):
    a = ns["remove_bg"](src, lo=225)
    fig = max(ns["figures"](a, 40), key=lambda f: f.shape[0] * f.shape[1])
    base = np.array(Image.open(ref)); ys = np.where(base[..., 3].any(1))[0]
    s = head_h(base[ys[0]:ys[-1] + 1]) / head_h(fig)
    h, w = fig.shape[:2]
    im = Image.fromarray(fig).resize((int(w * s), int(h * s)), Image.LANCZOS)
    sa = np.array(im).astype(int)
    blade = (sa[..., 3] > 0) & (sa[..., 2] > 150) & (sa[..., 2] > sa[..., 0] + 40)
    cols = np.where(((sa[..., 3] > 0) & ~blade).sum(0) > 40)[0]
    cx = (cols.min() + cols.max()) / 2
    can = Image.new("RGBA", (1536, 1536), (0, 0, 0, 0))
    can.paste(im, (int(768 - cx), 1227 - im.size[1]), im)
    can.save(out); print("scale", round(s, 3), "->", out)
    return can

if __name__ == "__main__":
    src, ref, out = sys.argv[1:4]
    can = place_pose(src, ref, out)
    prev = Image.new("RGBA", (1536 * 2, 1536), (60, 60, 70, 255))
    prev.alpha_composite(Image.open(ref), (0, 0)); prev.alpha_composite(can, (1536, 0))
    prev.resize((1536, 768)).crop((150, 150, 1386, 700)).save(os.path.join(HERE, "pose_compare.png"))
