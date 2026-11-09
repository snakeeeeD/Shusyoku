# 新プレイヤー（赤マント・水色の剣）のスプライト生成。
# 入力: work/{front,back,side}.png（1536四方、足元 y=1227、中心 x=768。prep.py が作る）
# 出力: out_sheets/player_{dir}_{idle|attack|skill|hurt}.png（4列x2行、1コマ=1536枠を縮小。attack 768、他 512）
#       out_sheets/player_{dir}.png（立ち絵 1024 = 枠の中央 256..1280）, out_sheets/player.png
import math, os
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "work")
OUT = os.path.join(HERE, "out_sheets")
os.makedirs(OUT, exist_ok=True)
CW = 1536
Y, X = np.mgrid[0:CW, 0:CW]
INK = (28, 20, 22, 255)


# ---------------- 小道具 ----------------
def dil(m, n=1):
    o = m.copy()
    for _ in range(n):
        p = o.copy(); o[1:, :] |= p[:-1, :]; o[:-1, :] |= p[1:, :]; o[:, 1:] |= p[:, :-1]; o[:, :-1] |= p[:, 1:]
    return o


def poly(pts):
    m = Image.new("L", (CW, CW), 0); ImageDraw.Draw(m).polygon(pts, fill=255); return np.asarray(m) > 0


def layer(arr, keep):
    return Image.fromarray(np.where(keep[..., None], arr, 0).astype(np.uint8), "RGBA")


def inpaint(arr, hole, n=80):
    """hole の画素を、周りの不透明な画素の色で外側から塗り埋める（腕を外した跡の穴埋め）"""
    out = arr.copy(); todo = hole.copy()
    ink = out[..., :3].max(axis=2) < 70                     # 輪郭の黒からは広げない（暗い四角になる）
    filled = (out[..., 3] > 0) & ~todo & ~ink
    for _ in range(n):
        if not todo.any(): break
        grow = np.zeros_like(todo)
        for sy, sx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            src = np.roll(filled, (sy, sx), axis=(0, 1)); col = np.roll(out, (sy, sx), axis=(0, 1))
            new = src & todo & ~grow
            out[new] = col[new]; grow |= new
        filled |= grow; todo &= ~grow
    out[todo] = 0
    return out


def extend_under(arr, cape, body, n=60):
    """マントの色を、体に隠れている部分まで塗り広げる（揺らした時の隙間防止）"""
    out = arr.copy(); filled = cape.copy(); target = body & ~cape
    for _ in range(n):
        grow = filled.copy()
        for sy, sx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            src = np.roll(filled, (sy, sx), axis=(0, 1)); col = np.roll(out, (sy, sx), axis=(0, 1))
            new = src & ~grow & target
            out[new] = col[new]; grow |= new
        if grow.sum() == filled.sum(): break
        filled = grow
    out[~filled] = 0
    return out, filled


def wave(lay, t, amp, top, bot, cx, sym, lam=520.0):
    a = np.asarray(lay).astype(np.float32)
    yy, xx = np.mgrid[0:CW, 0:CW].astype(np.float32)
    w = np.clip((yy - top) / max(1.0, bot - top), 0, 1) ** 1.4
    ph = 2 * math.pi * t - (yy - top) / lam * 2 * math.pi
    dx = amp * w * np.sin(ph)
    if sym: dx = dx * np.sign(xx - cx)
    sx = np.clip(xx - dx, 0, CW - 1.001); x0 = np.floor(sx).astype(int); fr = (sx - x0)[..., None]
    res = a[yy.astype(int), x0] * (1 - fr) + a[yy.astype(int), x0 + 1] * fr
    return Image.fromarray(np.clip(res, 0, 255).astype(np.uint8), "RGBA")


def rot_pt(p, c, deg):
    t = math.radians(deg); dx, dy = p[0] - c[0], p[1] - c[1]
    return (c[0] + dx * math.cos(t) + dy * math.sin(t), c[1] - dx * math.sin(t) + dy * math.cos(t))


def stretch(f, sy, feet, keep_volume=True):
    if abs(sy - 1.0) < 1e-4: return f
    sx = (1.0 / sy) if keep_volume else 1.0
    nw, nh = int(CW * sx), int(CW * sy)
    g = f.resize((nw, nh), Image.LANCZOS); o = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
    o.alpha_composite(g, ((CW - nw) // 2, feet - int(feet * sy)))
    return o


GLOW = (255, 232, 150)


def add_glow(f, k):
    if k <= 0.01: return f
    al = f.getchannel("A").filter(ImageFilter.GaussianBlur(30)).point(lambda v: int(min(255, v * 2.4 * k)))
    g = Image.new("RGBA", (CW, CW), GLOW + (0,)); g.putalpha(al)
    o = Image.new("RGBA", (CW, CW), (0, 0, 0, 0)); o.alpha_composite(g); o.alpha_composite(f); return o


SPARK = [(-300, 160, 34, 40), (290, 80, 40, 34), (-250, -120, 30, 30), (320, 260, 46, 38), (-340, 330, 38, 32),
         (250, -60, 44, 36), (-200, 420, 36, 28), (360, -150, 32, 30), (-380, 20, 50, 34), (200, 380, 42, 30)]


def add_sparks(f, i, k, center):
    if k <= 0.01: return f
    lay = Image.new("RGBA", (CW, CW), (0, 0, 0, 0)); d = ImageDraw.Draw(lay)
    for (ox, oy, spd, sz) in SPARK:
        x = center[0] + ox; y = center[1] + oy - spd * i; a = int(255 * k); s = sz * (0.7 + 0.3 * k)
        d.polygon([(x, y - s), (x + s * 0.25, y), (x, y + s), (x - s * 0.25, y)], fill=(255, 250, 215, a))
        d.polygon([(x - s, y), (x, y - s * 0.25), (x + s, y), (x, y + s * 0.25)], fill=(255, 250, 215, a))
    o = f.copy(); o.alpha_composite(lay.filter(ImageFilter.GaussianBlur(10))); o.alpha_composite(lay); return o


# ---------------- 顔 ----------------
def skin_near(arr, cx, cy, r):
    m = ((X - cx) ** 2 + (Y - cy) ** 2 < r * r) & (arr[..., 3] > 200) & (arr[..., 0] > 225) & (arr[..., 1] > 180) & (arr[..., 2] > 150)
    c = np.median(arr[m][:, :3], axis=0).astype(int); return (int(c[0]), int(c[1]), int(c[2]), 255)


def x_eye(d, cx, cy, w, h, point_right, width=8):
    pts = [(cx - w, cy - h), (cx + w, cy), (cx - w, cy + h)] if point_right else [(cx + w, cy - h), (cx - w, cy), (cx + w, cy + h)]
    d.line(pts, fill=INK, width=width, joint="curve")


def face_front(img, kind):
    arr = np.asarray(img).copy(); sk = skin_near(arr, 775, 690, 30)
    im = Image.fromarray(arr, "RGBA"); d = ImageDraw.Draw(im)
    if kind == "strain":   # 力み：目は元のまま（にらんだ目）、口だけ食いしばる
        d.rounded_rectangle([760, 698, 792, 712], radius=5, fill=(255, 255, 255, 255), outline=(90, 30, 35, 255), width=3)
        d.line([(764, 705), (788, 705)], fill=(90, 30, 35, 255), width=2)
        return im
    for (x0, y0, x1, y1) in ((708, 645, 752, 688), (798, 645, 840, 688)):     # 目を肌で消す
        d.ellipse([x0, y0, x1, y1], fill=sk)
    if True:               # 痛い：＞ ＜ と開いた口
        x_eye(d, 731, 667, 13, 12, True); x_eye(d, 818, 667, 13, 12, False)
        d.rounded_rectangle([762, 696, 792, 718], radius=7, fill=(120, 34, 44, 255), outline=(70, 20, 25, 255), width=3)
    return im


def face_side(img, kind):
    arr = np.asarray(img).copy(); sk = skin_near(arr, 925, 690, 22)
    im = Image.fromarray(arr, "RGBA"); d = ImageDraw.Draw(im)
    if kind == "strain":   # 力み：目は元のまま、口だけ食いしばる
        d.line([(936, 694), (952, 692)], fill=(90, 30, 35, 255), width=4)
        return im
    d.ellipse([898, 648, 942, 684], fill=sk)                                     # 目を肌で消す
    if True:
        x_eye(d, 920, 666, 12, 11, True)
        d.ellipse([936, 688, 952, 702], fill=(120, 34, 44, 255), outline=(70, 20, 25, 255), width=2)
    return im


# ---------------- リグ ----------------
def red_mask(a):
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    # マントの赤（明るい赤〜暗い赤）。茶色（緑>青）・肌（緑が高い）は除く
    m = (al > 60) & (r > 70) & (r < 235) & (g < 95) & (r > g + 35) & (r > b + 20) & (b >= g + 1)
    return keep_big(m, 3000)


def keep_big(m, min_px):
    """小さな塊（髪や金具に混じった赤っぽい点）を除き、大きな塊（マント・スカーフ）だけ残す"""
    from collections import deque
    H, W = m.shape; seen = np.zeros_like(m); out = np.zeros_like(m)
    ys, xs = np.nonzero(m)
    for y0, x0 in zip(ys, xs):
        if seen[y0, x0]: continue
        q = deque([(y0, x0)]); seen[y0, x0] = True; pts = []
        while q:
            y, x = q.popleft(); pts.append((y, x))
            for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                ny, nx = y + dy, x + dx
                if 0 <= ny < H and 0 <= nx < W and m[ny, nx] and not seen[ny, nx]:
                    seen[ny, nx] = True; q.append((ny, nx))
        if len(pts) >= min_px:
            py, px = zip(*pts); out[list(py), list(px)] = True
    return out


class Rig:
    def __init__(self, name, arm_poly, piv, torso_hole=None, sym=False, faces=None):
        base = Image.open(os.path.join(WORK, name + ".png")).convert("RGBA")
        a = np.asarray(base).copy(); al = a[..., 3] > 16
        self.base = base
        red = dil(red_mask(a.astype(int)), 2)
        arm = poly(arm_poly) & al & ~red
        arm = dil(arm, 2) & al & ~red
        cape = dil(red_mask(a.astype(int)), 4) & al & ~arm
        self.arm = arm; self.cape = cape
        # 腕を外した後の体（腕に隠れていた胴を塗り埋め）
        body_arr = a.copy()
        if torso_hole is not None:
            hole = arm & poly(torso_hole)
            tz = poly(torso_hole) & ~arm & (a[..., 3] > 200)
            navy = tz & (a[..., 2] > a[..., 0] + 15) & (a[..., 2] > 60)
            src = navy if navy.sum() > 50 else (al & ~arm & (a[..., 2] > a[..., 0] + 15) & (a[..., 2] > 60))
            col = np.median(a[src][:, :3], axis=0).astype(np.uint8)
            body_arr[hole] = np.append(col, 255)               # 腕に隠れていた胴は、上着の色で埋める
            body_keep = (body_arr[..., 3] > 0) & ~(arm & ~hole) & ~cape
        else:
            body_keep = al & ~arm & ~cape
        self.body_arr = body_arr; self.body_keep = body_keep
        ext, filled = extend_under(a, cape, al & ~arm)
        self.CAPE = layer(ext, filled)
        self.ARM = layer(a, arm)
        self.BODY = layer(body_arr, body_keep)
        self.ALT = {}
        if faces:
            for k in ("strain", "pain"):
                fa = np.asarray(faces(base, k)).copy()
                if torso_hole is not None: fa = np.where((arm & poly(torso_hole))[..., None], body_arr, fa)
                self.ALT[k] = layer(fa, body_keep)
        ys, xs = np.nonzero(cape)
        self.top, self.bot = ys.min(), ys.max()
        ay, ax = np.nonzero(al)
        self.cx = 768; self.FEET = ay.max(); self.HIP = (768, ay.max() - 60)
        self.SH = piv; self.sym = sym
        self.center = (768, int((ay.min() + ay.max()) / 2))

    def put_arm(self, f, aa, ab, dx, dy, alpha=1.0):
        sh2 = rot_pt(self.SH, self.HIP, ab)
        lay = self.ARM.rotate(aa + ab, resample=Image.BICUBIC, center=self.SH)
        if alpha < 1.0:
            arr = np.asarray(lay).copy(); arr[..., 3] = (arr[..., 3] * alpha).astype(np.uint8); lay = Image.fromarray(arr, "RGBA")
        f.alpha_composite(lay, (int(round(sh2[0] - self.SH[0] + dx)), int(round(sh2[1] - self.SH[1] + dy))))

    def frame(self, t, aa=0, ab=0, dx=0, dy=0, sy=1.0, amp=12.0, front=1, face=None, ghost=None):
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        cape = wave(self.CAPE, t, amp, self.top, self.bot, self.cx, self.sym)
        f.alpha_composite(cape.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        if not front: self.put_arm(f, aa, ab, dx, dy)
        body = self.ALT.get(face, self.BODY) if face else self.BODY
        f.alpha_composite(body.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        if ghost is not None:                                   # 残像（振りのブレ）
            for k, tt in enumerate((0.25, 0.5, 0.75)):
                self.put_arm(f, ghost + (aa - ghost) * tt, ab, dx, dy, alpha=0.16 + 0.08 * k)
        if front: self.put_arm(f, aa, ab, dx, dy)
        return stretch(f, sy, self.FEET)


def sheet(frames, path, fc):
    sh = Image.new("RGBA", (fc * 4, fc * 2), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.alpha_composite(f.resize((fc, fc), Image.LANCZOS), (fc * (i % 4), fc * (i // 4)))
    sh.save(path); print("saved", os.path.basename(path), sh.size)


# ---------------- 動き ----------------
# attack: (腕, 体の傾き, 横, 上下, 伸縮, 力み顔, 残像の元, 腕が手前)
ATTACK = {
    # 横：水平に構えた剣を、頭の上へ振りかぶって前下へ斬り下ろす
    "side": [(0, 0, 0, 0, 1.00, 0, None, 1), (30, 3, -8, 0, 1.00, 1, None, 1), (62, 5, -14, 0, 0.97, 1, None, 1),
             (78, 6, -18, 0, 0.94, 1, None, 1), (25, -2, 8, 0, 1.04, 1, 78, 1), (-28, -6, 24, 0, 1.01, 1, 25, 1),
             (-48, -8, 30, 0, 0.95, 1, -28, 1), (-40, -5, 24, 0, 1.00, 1, None, 1)],
    # 前：剣側（画面左）の外へ高く振り上げ、体の前を斜めに斬り下ろす
    "front": [(0, 0, 0, 0, 1.00, 0, None, 1), (-40, -2, 0, 0, 1.00, 1, None, 1), (-88, -3, 0, 2, 0.97, 1, None, 1),
              (-112, -4, 0, 4, 0.94, 1, None, 1), (-40, 1, 0, -2, 1.04, 1, -112, 1), (25, 3, 0, -6, 1.01, 1, -40, 1),
              (58, 4, 0, -8, 0.95, 1, 25, 1), (50, 3, 0, -6, 1.00, 1, None, 1)],
    # 後：前の左右反対
    "back": [(0, 0, 0, 0, 1.00, 0, None, 1), (40, 2, 0, 0, 1.00, 0, None, 1), (88, 3, 0, -2, 0.97, 0, None, 1),
             (112, 4, 0, -4, 0.94, 0, None, 1), (40, -1, 0, 2, 1.04, 0, 112, 1), (-25, -3, 0, 6, 1.01, 0, 40, 1),
             (-58, -4, 0, 8, 0.95, 0, -25, 1), (-50, -3, 0, 6, 1.00, 0, None, 1)],
}
# skill: (腕, 体, 上下, 伸縮, 光, マント)  ＝剣を掲げて光る
SKILL = {
    "side": [(0, 0, 0, 1.00, 0.00, 1.0), (-8, 2, 4, 0.97, 0.10, 1.2), (-10, 3, 6, 0.95, 0.25, 1.4), (25, 0, -4, 1.02, 0.45, 2.0),
             (50, -1, -8, 1.04, 0.70, 2.6), (60, -1, -10, 1.05, 0.95, 3.0), (60, -1, -10, 1.05, 1.00, 3.0), (30, 0, -4, 1.02, 0.55, 2.0)],
    "front": [(0, 0, 0, 1.00, 0.00, 1.0), (10, 1, 4, 0.97, 0.10, 1.2), (12, 1, 6, 0.95, 0.25, 1.4), (-50, 0, -4, 1.02, 0.45, 2.0),
              (-95, 0, -8, 1.04, 0.70, 2.6), (-125, 0, -10, 1.05, 0.95, 3.0), (-125, 0, -10, 1.05, 1.00, 3.0), (-60, 0, -4, 1.02, 0.55, 2.0)],
    "back": [(0, 0, 0, 1.00, 0.00, 1.0), (-10, -1, 4, 0.97, 0.10, 1.2), (-12, -1, 6, 0.95, 0.25, 1.4), (50, 0, -4, 1.02, 0.45, 2.0),
             (95, 0, -8, 1.04, 0.70, 2.6), (125, 0, -10, 1.05, 0.95, 3.0), (125, 0, -10, 1.05, 1.00, 3.0), (60, 0, -4, 1.02, 0.55, 2.0)],
}
# hurt: (腕, 体, 横, 上下, 伸縮, マント, 痛い顔)
HURT = {
    "side": [(12, 8, -18, 0, 0.92, 1.6, 1), (18, 12, -28, 0, 0.95, 1.8, 1), (15, 10, -24, 0, 0.98, 1.6, 1), (10, 6, -16, 0, 1.00, 1.4, 1),
             (6, 3, -9, 0, 1.01, 1.2, 1), (3, 1, -4, 0, 1.00, 1.1, 0), (1, 0, -1, 0, 1.00, 1.0, 0), (0, 0, 0, 0, 1.00, 1.0, 0)],
    "front": [(-10, 3, 0, -8, 0.92, 1.6, 1), (-14, -4, 0, -14, 0.95, 1.8, 1), (-10, 3, 0, -12, 0.98, 1.6, 1), (-6, -2, 0, -8, 1.00, 1.4, 1),
              (-3, 1, 0, -4, 1.01, 1.2, 1), (-1, 0, 0, -2, 1.00, 1.1, 0), (0, 0, 0, 0, 1.00, 1.0, 0), (0, 0, 0, 0, 1.00, 1.0, 0)],
    "back": [(10, -3, 0, 8, 0.92, 1.6, 0), (14, 4, 0, 14, 0.95, 1.8, 0), (10, -3, 0, 12, 0.98, 1.6, 0), (6, 2, 0, 8, 1.00, 1.4, 0),
             (3, -1, 0, 4, 1.01, 1.2, 0), (1, 0, 0, 2, 1.00, 1.1, 0), (0, 0, 0, 0, 1.00, 1.0, 0), (0, 0, 0, 0, 1.00, 1.0, 0)],
}

if __name__ == "__main__":
    rigs = {
        "side": Rig("side", [(832, 768), (905, 766), (1000, 760), (1370, 690), (1385, 870), (1060, 905), (960, 905), (880, 875), (832, 855)],
                    (818, 800), torso_hole=[(820, 760), (880, 760), (880, 880), (820, 880)], faces=face_side),
        "front": Rig("front", [(640, 790), (703, 792), (712, 880), (700, 960), (665, 1010), (600, 1110), (545, 1245), (465, 1245),
                               (495, 1080), (555, 960), (598, 880)], (680, 815), sym=True, faces=face_front),
        "back": Rig("back", [(836, 784), (882, 788), (908, 878), (930, 928), (1000, 1172), (978, 1194), (900, 1012), (858, 952), (836, 880)],
                    (858, 806), sym=True),
    }
    seqs = {}
    for name, rig in rigs.items():
        fr = []
        for i, (aa, ab, dx, dy, sy, st, gh, fnt) in enumerate(ATTACK[name]):
            fr.append(rig.frame(i / 8, aa, ab, dx, dy, sy, 14.0, fnt, "strain" if st else None, gh))
        sheet(fr, os.path.join(OUT, f"player_{name}_attack.png"), 768); seqs[(name, "attack")] = fr
        fr = [rig.frame(i / 8, sy=1.0 + 0.010 * math.sin(2 * math.pi * i / 8), amp=11.0 + 4.0 * (0.5 + 0.5 * math.sin(2 * math.pi * i / 8 + 1.0)))
              for i in range(8)]
        sheet(fr, os.path.join(OUT, f"player_{name}_idle.png"), 512); seqs[(name, "idle")] = fr
        fr = []
        for i, (aa, ab, dy, sy, k, amp) in enumerate(SKILL[name]):
            f = rig.frame(i / 8, aa, ab, 0, dy, sy, 12.0 * amp, 1)
            fr.append(add_sparks(add_glow(f, k), i, k, rig.center))
        sheet(fr, os.path.join(OUT, f"player_{name}_skill.png"), 512); seqs[(name, "skill")] = fr
        fr = []
        for i, (aa, ab, dx, dy, sy, amp, pain) in enumerate(HURT[name]):
            fr.append(rig.frame(i / 8, aa, ab, dx, dy, sy, 12.0 * amp, 1, "pain" if pain else None))
        sheet(fr, os.path.join(OUT, f"player_{name}_hurt.png"), 512); seqs[(name, "hurt")] = fr
        # 立ち絵（1024 = 枠の中央）
        rig.base.crop((256, 256, 1280, 1280)).save(os.path.join(OUT, f"player_{name}.png"))

    # player.png（旧と同じ 1254 四方・ほぼ全高）：正面
    fb = rigs["front"].base; al = np.asarray(fb)[..., 3] > 16; ys = np.where(al.any(1))[0]; xs = np.where(al.any(0))[0]
    c = fb.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)); s = 1233 / c.size[1]
    c = c.resize((int(c.size[0] * s), 1233), Image.LANCZOS)
    p = Image.new("RGBA", (1254, 1254), (0, 0, 0, 0)); p.alpha_composite(c, ((1254 - c.size[0]) // 2, 10)); p.save(os.path.join(OUT, "player.png"))

    # プレビュー：アニメごとに 3方向 x 8コマ
    for anim in ("attack", "idle", "skill", "hurt"):
        T = 200
        strip = Image.new("RGB", (T * 8, T * 3), (58, 54, 70))
        for k, name in enumerate(("side", "front", "back")):
            for i, f in enumerate(seqs[(name, anim)]):
                strip.paste(f.resize((T, T), Image.LANCZOS).convert("RGB"), (T * i, T * k), f.resize((T, T), Image.LANCZOS))
        strip.save(os.path.join(HERE, f"prev_{anim}.png"))
        gif = []
        for i in range(8):
            c = Image.new("RGBA", (300 * 3, 300), (58, 54, 70, 255))
            for k, name in enumerate(("side", "front", "back")):
                c.alpha_composite(seqs[(name, anim)][i].resize((300, 300), Image.LANCZOS), (300 * k, 0))
            gif.append(c.convert("RGB"))
        dur = {"attack": [90, 70, 70, 120, 50, 50, 60, 200], "idle": 160, "skill": [80, 90, 100, 80, 80, 90, 220, 140],
               "hurt": [50, 70, 70, 70, 70, 70, 60, 300]}[anim]
        gif[0].save(os.path.join(HERE, f"prev_{anim}.gif"), save_all=True, append_images=gif[1:], duration=dur, loop=0)
