# 新しいプレイヤー絵の下ごしらえ：背景を抜き、アニメ作業用の 1536 四方の枠に置く。
# 規格：旧立ち絵(1024, 身長≈797px, 足元 y=971) を PAD256 で 1536 に置いた位置＝足元 y=1227, 中心 x=768
import numpy as np
from PIL import Image
from collections import deque
import os

HERE = os.path.dirname(os.path.abspath(__file__))
IMG = os.path.join(HERE, "src")
OUT = os.path.join(HERE, "work")
os.makedirs(OUT, exist_ok=True)
CW = 1536; FEET = 1227; CX = 768; TALL = 797


def remove_bg(path, lo=200):
    a = np.array(Image.open(path).convert("RGB")).astype(np.int16)
    H, W, _ = a.shape
    mx = a.max(2); mn = a.min(2)
    bgc = (mn >= lo) & ((mx - mn) <= 14)
    bg = np.zeros((H, W), bool); q = deque()
    for x in range(W):
        for y in (0, H - 1):
            if bgc[y, x]: bg[y, x] = True; q.append((y, x))
    for y in range(H):
        for x in (0, W - 1):
            if bgc[y, x] and not bg[y, x]: bg[y, x] = True; q.append((y, x))
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and bgc[ny, nx] and not bg[ny, nx]:
                bg[ny, nx] = True; q.append((ny, nx))
    # 囲まれたすき間：市松（明るい灰と少し暗い灰が混ざる）か、真っ白の塊なら抜く
    lab = np.zeros((H, W), np.int32); n = 0
    for y0 in range(H):
        for x0 in range(W):
            if bgc[y0, x0] and not bg[y0, x0] and lab[y0, x0] == 0:
                n += 1; lab[y0, x0] = n; q = deque([(y0, x0)]); pts = []
                while q:
                    y, x = q.popleft(); pts.append((y, x))
                    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        ny, nx = y + dy, x + dx
                        if 0 <= ny < H and 0 <= nx < W and bgc[ny, nx] and not bg[ny, nx] and lab[ny, nx] == 0:
                            lab[ny, nx] = n; q.append((ny, nx))
                ys = np.array([p[0] for p in pts]); xs = np.array([p[1] for p in pts]); v = mn[ys, xs]
                checker = (v >= 242).mean() > 0.2 and (v <= 236).mean() > 0.2
                white = (v >= 246).mean() > 0.9 and len(pts) >= 150
                if len(pts) >= 30 and (checker or white):
                    bg[ys, xs] = True
    alpha = np.where(bg, 0, 255).astype(np.uint8)
    return np.dstack([a.astype(np.uint8), alpha])


def figures(rgba, min_w=40):
    fg = rgba[..., 3] > 0
    cols = fg.any(0); segs = []; x = 0; W = fg.shape[1]
    while x < W:
        if cols[x]:
            s = x
            while x < W and cols[x]: x += 1
            if x - s > min_w: segs.append((s, x))
        x += 1
    out = []
    for s, e in segs:
        ys = np.where(fg[:, s:e].any(1))[0]
        out.append(rgba[ys[0]:ys[-1] + 1, s:e])
    return out


def hair_h(fig):
    """髪（こげ茶）の上端から、髪の塊の下端までの高さ＝頭の大きさの目安"""
    a = fig.astype(int); r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    hair = (al > 0) & (r > 60) & (r < 140) & (g > 35) & (g < 95) & (b > 25) & (b < 80) & (r > g + 15)
    rows = np.where(hair.sum(1) > 12)[0]
    top = rows[0]
    # 最初の連続した髪の塊（頭）だけ
    end = top
    for y in rows:
        if y - end > 6: break
        end = y
    return end - top


def place(fig, scale, body_cx=None):
    h, w = fig.shape[:2]
    im = Image.fromarray(fig).resize((int(round(w * scale)), int(round(h * scale))), Image.LANCZOS)
    can = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
    cx = (body_cx * scale) if body_cx is not None else im.size[0] / 2
    can.paste(im, (int(round(CX - cx)), FEET - im.size[1]), im)
    return can


a1 = remove_bg(os.path.join(IMG, "three_views.webp"))
front, back, side_old = figures(a1)[:3]
a3 = remove_bg(os.path.join(IMG, "side_stance.webp"), lo=225)
side = figures(a3)[0]

s1 = TALL / max(front.shape[0], back.shape[0])
def head_h(fig):   # top of hair -> top of the red scarf
    a = fig.astype(int); al = a[..., 3] > 0
    red = al & (a[..., 0] > 140) & (a[..., 1] < 70) & (a[..., 2] < 80)
    return np.where(red.any(1))[0][0] - np.where(al.any(1))[0][0]
hf, hs = head_h(front), head_h(side)
s3 = s1 * hf / hs
print("scale front/back", round(s1, 3), "side", round(s3, 3), "head", hf, hs)

# 横向きの体の中心：剣を除いた（剣が占めない）列の中心
sa = side.astype(int)
blade = (sa[..., 3] > 0) & (sa[..., 2] > 170) & (sa[..., 2] > sa[..., 0] + 40)
body_cols = np.where(((sa[..., 3] > 0) & ~blade).sum(0) > 60)[0]
body_cx = (body_cols.min() + body_cols.max()) / 2
print("side body cx", body_cx, "of", side.shape[1])

place(front, s1).save(os.path.join(OUT, "front.png"))
place(back, s1).save(os.path.join(OUT, "back.png"))
place(side, s3, body_cx).save(os.path.join(OUT, "side.png"))

prev = Image.new("RGBA", (CW * 3, CW), (60, 60, 70, 255))
for i, n in enumerate(["front", "back", "side"]):
    im = Image.open(os.path.join(OUT, n + ".png")); prev.alpha_composite(im, (i * CW, 0))
from PIL import ImageDraw
d = ImageDraw.Draw(prev)
for i in range(3):
    d.line([(i * CW + 256, FEET), (i * CW + 1280, FEET)], fill=(255, 80, 80, 255), width=3)
    d.rectangle([i * CW + 256, 256, i * CW + 1280, 1280], outline=(120, 200, 255, 255), width=3)
prev.resize((CW * 3 // 3, CW // 3)).save(os.path.join(HERE, "work_preview.png"))
