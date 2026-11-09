# 新プレイヤーのスプライト（16コマ版）。make_sprites.py の部品（Rig・顔・光など）を使い、
# キーフレームを補間して 16 コマを作る。1コマ 384（1536枠の 1/4）、4列x4行。
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sprites as M
from make_sprites import CW, X, Y, poly, dil, rot_pt, stretch, add_glow, add_sparks, face_front, face_side

HERE = M.HERE
OUT = os.path.join(HERE, "out16")
os.makedirs(OUT, exist_ok=True)
N = 16
FC = 384


class Rig2(M.Rig):
    def __init__(self, *a, head_line=None, **k):
        super().__init__(*a, **k)
        self.HEAD = None
        if head_line is not None:   # 頭（首より上）を最前面に描き直す＝奥へ振る剣が頭の向こうを通る
            al = np.asarray(self.BODY)[..., 3] > 0
            self.HEAD = M.layer(np.asarray(self.BODY), al & (Y < head_line))
        # 腕の元の向き（肩→剣先）。奥行きの短縮はこの軸に沿って縮める
        ys, xs = np.nonzero(self.arm)
        d2 = (xs - self.SH[0]) ** 2 + (ys - self.SH[1]) ** 2
        tip = (xs[d2.argmax()], ys[d2.argmax()])
        self.th0 = math.degrees(math.atan2(-(tip[1] - self.SH[1]), tip[0] - self.SH[0]))

    def arm_layer(self, ls):
        if abs(ls - 1.0) < 1e-3: return self.ARM
        a = 90.0 - self.th0
        v = self.ARM.rotate(a, resample=Image.BICUBIC, center=self.SH)           # 腕を真上に向けて
        hh = max(1, int(CW * ls)); v2 = v.resize((CW, hh), Image.LANCZOS)        # 縦だけ伸縮（肩は固定）
        top = int(round(self.SH[1] - self.SH[1] * ls))
        c = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        if top >= 0: c.alpha_composite(v2, (0, top))
        else: c.alpha_composite(v2.crop((0, -top, CW, -top + CW)), (0, 0))
        return c.rotate(-a, resample=Image.BICUBIC, center=self.SH)

    def put_arm2(self, f, aa, ab, dx, dy, ls=1.0, alpha=1.0, adx=0.0, ady=0.0):
        sh2 = rot_pt(self.SH, self.HIP, ab)
        lay = self.arm_layer(ls).rotate(aa + ab, resample=Image.BICUBIC, center=self.SH)
        if alpha < 1.0:
            arr = np.asarray(lay).copy(); arr[..., 3] = (arr[..., 3] * alpha).astype(np.uint8); lay = Image.fromarray(arr, "RGBA")
        f.alpha_composite(lay, (int(round(sh2[0] - self.SH[0] + dx + adx)), int(round(sh2[1] - self.SH[1] + dy + ady))))

    def frame2(self, t, p, ghost=None):
        aa, ab, dx, dy, sy, ls = p["aa"], p["ab"], p["dx"], p["dy"], p["sy"], p["ls"]
        adx, ady = p["adx"], p["ady"]   # 腕ごとずらす（奥へ腕を伸ばす＝画面の上へ）
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        cape = M.wave(self.CAPE, t, p["amp"], self.top, self.bot, self.cx, self.sym)
        f.alpha_composite(cape.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        behind = p["front"] < 0.5
        if behind: self.put_arm2(f, aa, ab, dx, dy, ls, adx=adx, ady=ady)
        face = p.get("face")
        body = self.ALT.get(face, self.BODY) if face else self.BODY
        f.alpha_composite(body.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        if ghost is not None:
            gaa, gls, gdx, gdy = ghost
            for k, tt in enumerate((0.2, 0.4, 0.6, 0.8)):
                self.put_arm2(f, gaa + (aa - gaa) * tt, ab, dx, dy, gls + (ls - gls) * tt, alpha=0.12 + 0.06 * k,
                              adx=gdx + (adx - gdx) * tt, ady=gdy + (ady - gdy) * tt)
        if not behind: self.put_arm2(f, aa, ab, dx, dy, ls, adx=adx, ady=ady)
        if self.HEAD is not None and p.get("head", 0) > 0.5:
            f.alpha_composite(self.HEAD.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        f = stretch(f, sy, self.FEET)
        if p.get("glow", 0) > 0.01:
            f = add_sparks(add_glow(f, p["glow"]), p["_i"] * 0.5, p["glow"], self.center)
        return f


DEF = dict(aa=0.0, ab=0.0, dx=0.0, dy=0.0, sy=1.0, ls=1.0, amp=12.0, front=1.0, glow=0.0, face=None, head=0, adx=0.0, ady=0.0)


def ease(u): return u * u * (3 - 2 * u)


def expand(keys, n=N):
    """keys: [(コマ番号, {値}), ...] → n コマぶんの値（間はなめらかに補間。顔・手前・頭は直前の値）"""
    out = []
    keys = [(i, dict(DEF, **k)) for i, k in keys]
    for f in range(n):
        for j in range(len(keys) - 1):
            if keys[j][0] <= f <= keys[j + 1][0]: break
        (i0, a), (i1, b) = keys[j], keys[j + 1]
        u = 0.0 if i1 == i0 else ease((f - i0) / (i1 - i0))
        p = {}
        for k in a:
            if isinstance(a[k], (int, float)) and k not in ("front", "head") and isinstance(b.get(k, a[k]), (int, float)):
                p[k] = a[k] + (b[k] - a[k]) * u
            else:
                p[k] = a[k] if f < i1 else b[k]
        p["_i"] = f
        out.append(p)
    return out


def render(rig, keys, path, ghost_min=14.0, loop_t=True):
    ps = expand(keys)
    frames = []
    for i, p in enumerate(ps):
        g = None
        if i > 0:
            q = ps[i - 1]
            if abs(q["aa"] - p["aa"]) >= ghost_min or abs(q["ls"] - p["ls"]) >= 0.25 or abs(q["ady"] - p["ady"]) >= 40:
                g = (q["aa"], q["ls"], q["adx"], q["ady"])
        frames.append(rig.frame2(i / N if loop_t else i / N * 2, p, g))
    sh = Image.new("RGBA", (FC * 4, FC * 4), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.alpha_composite(f.resize((FC, FC), Image.LANCZOS), (FC * (i % 4), FC * (i // 4)))
    sh.save(path); print("saved", os.path.basename(path), sh.size)
    return frames


# ---------------- 動き（キーフレーム） ----------------
# aa=腕の回転(反時計回り+) ab=体の傾き(反時計回り+) dx/dy=体の移動(px, 1536枠) sy=縦の伸縮 ls=腕の長さ(奥へ短く/手前へ長く)
ATTACK = {
    # 横：大きく後ろへ振りかぶり → 大きく踏み込んで前下へ斬り下ろす
    "side": [(0, {}),
             (4, dict(aa=95, ab=12, dx=-40, sy=0.90, face="strain", amp=18)),          # 振りかぶり（後ろへ反って沈む）
             (6, dict(aa=105, ab=14, dx=-46, sy=0.88, face="strain", amp=18)),         # 溜め
             (8, dict(aa=10, ab=-8, dx=60, sy=1.08, face="strain", amp=30)),           # 踏み込んで振る
             (10, dict(aa=-55, ab=-16, dx=95, sy=0.94, face="strain", amp=34)),        # 振り切り（前へ倒れ込む）
             (12, dict(aa=-50, ab=-12, dx=85, sy=0.97, face="strain", amp=26)),
             (15, dict(aa=0, ab=0, dx=0, sy=1.0, amp=14))],
    # 前：外側へ高く振り上げ → 手前（敵の方）へ踏み込みながら斜めに斬り下ろす。剣は手前へ伸びる
    "front": [(0, {}),
              (4, dict(aa=-125, ab=-6, dy=-24, sy=0.90, face="strain", amp=18)),
              (6, dict(aa=-135, ab=-7, dy=-30, sy=0.88, face="strain", amp=18)),
              (8, dict(aa=-30, ab=4, dy=30, sy=1.08, ls=1.15, face="strain", amp=30)),
              (10, dict(aa=62, ab=8, dy=60, sy=0.95, ls=1.2, face="strain", amp=34)),
              (12, dict(aa=55, ab=6, dy=55, sy=0.98, ls=1.15, face="strain", amp=26)),
              (15, dict(aa=0, ab=0, dy=0, sy=1.0, ls=1.0, amp=14))],
    # 後：剣を右から頭上へ振りかぶり → 奥（敵の方＝画面の上）へ踏み込み、頭の向こうへ斬り下ろす
    "back": [(0, dict(head=1)),
             (4, dict(aa=120, ab=8, dy=26, sy=0.90, ls=1.1, amp=18, head=1)),           # 右上へ振りかぶる（手前へ反る＝剣は手前に長い）
             (6, dict(aa=140, ab=9, dy=30, sy=0.88, ls=1.15, amp=18, head=1)),
             (8, dict(aa=160, ab=-2, dy=-40, sy=1.06, ls=1.0, ady=-150, adx=-20, amp=30, head=1, front=0)),  # 奥へ踏み込み、腕を前（画面の上）へ伸ばす
             (10, dict(aa=185, ab=-4, dy=-80, sy=0.96, ls=0.9, ady=-270, adx=-70, amp=34, head=1, front=0)), # 振り切り：頭の向こう（敵の方）で振り下ろす
             (12, dict(aa=182, ab=-3, dy=-72, sy=0.98, ls=0.9, ady=-255, adx=-65, amp=26, head=1, front=0)),
             (13, dict(aa=140, ab=-1, dy=-40, sy=1.0, ls=1.0, ady=-80, amp=18, head=1)),
             (15, dict(aa=0, ab=0, dy=0, sy=1.0, ls=1.0, amp=14, head=1))],
}
SKILL = {
    # 沈んでから跳ねるように伸び、剣を高く掲げて光る
    "side": [(0, {}), (3, dict(aa=-15, ab=6, dy=30, sy=0.86, amp=16, glow=0.15)),
             (6, dict(aa=70, ab=-4, dy=-50, sy=1.10, amp=32, glow=0.6)),
             (9, dict(aa=85, ab=-3, dy=-60, sy=1.08, amp=40, glow=1.0)),
             (12, dict(aa=85, ab=-3, dy=-55, sy=1.06, amp=36, glow=1.0)),
             (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=14, glow=0.2))],
    "front": [(0, {}), (3, dict(aa=15, ab=3, dy=30, sy=0.86, amp=16, glow=0.15)),
              (6, dict(aa=-90, ab=-3, dy=-50, sy=1.10, amp=32, glow=0.6)),
              (9, dict(aa=-130, ab=-3, dy=-60, sy=1.08, amp=40, glow=1.0)),
              (12, dict(aa=-130, ab=-3, dy=-55, sy=1.06, amp=36, glow=1.0)),
              (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=14, glow=0.2))],
    "back": [(0, dict(head=1)), (3, dict(aa=-15, ab=-3, dy=30, sy=0.86, amp=16, glow=0.15, head=1)),
             (6, dict(aa=90, ab=3, dy=-50, sy=1.10, amp=32, glow=0.6, head=1)),
             (9, dict(aa=135, ab=3, dy=-60, sy=1.08, amp=40, glow=1.0, head=1)),
             (12, dict(aa=135, ab=3, dy=-55, sy=1.06, amp=36, glow=1.0, head=1)),
             (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=14, glow=0.2, head=1))],
}
HURT = {
    # 大きくのけぞって飛ばされ、よろけて戻る
    "side": [(0, {}), (2, dict(aa=25, ab=22, dx=-70, sy=0.86, face="pain", amp=34)),
             (5, dict(aa=30, ab=26, dx=-90, sy=0.92, face="pain", amp=36)),
             (9, dict(aa=12, ab=8, dx=-40, sy=1.02, face="pain", amp=22)),
             (12, dict(aa=-6, ab=-5, dx=-10, sy=1.0, amp=16)),
             (15, dict(aa=0, ab=0, dx=0, sy=1.0, amp=12))],
    "front": [(0, {}), (2, dict(aa=-20, ab=-10, dy=-50, sy=0.86, face="pain", amp=34)),
              (5, dict(aa=-28, ab=10, dy=-70, sy=0.92, face="pain", amp=36)),
              (9, dict(aa=-10, ab=-6, dy=-30, sy=1.02, face="pain", amp=22)),
              (12, dict(aa=4, ab=3, dy=-6, sy=1.0, amp=16)),
              (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=12))],
    "back": [(0, dict(head=1)), (2, dict(aa=20, ab=10, dy=50, sy=0.86, amp=34, head=1)),
             (5, dict(aa=28, ab=-10, dy=70, sy=0.92, amp=36, head=1)),
             (9, dict(aa=10, ab=6, dy=30, sy=1.02, amp=22, head=1)),
             (12, dict(aa=-4, ab=-3, dy=6, sy=1.0, amp=16, head=1)),
             (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=12, head=1))],
}


def idle_keys(head):
    ks = []
    for i in range(N):
        t = i / N
        b = math.sin(2 * math.pi * t)
        ks.append((i, dict(sy=1.0 + 0.010 * b, dy=-2 * b, ab=0.4 * math.sin(2 * math.pi * t + 0.8),   # 待機は控えめに
                           aa=1.5 * math.sin(2 * math.pi * t + 1.2), amp=11 + 3 * (0.5 + 0.5 * math.sin(2 * math.pi * t + 1.0)),
                           head=head)))
    return ks


if __name__ == "__main__":
    CFG = {
        "side": dict(args=("side", [(832, 768), (905, 766), (1000, 760), (1370, 690), (1385, 870), (1060, 905), (960, 905), (880, 875), (832, 855)], (818, 800)),
                     kw=dict(torso_hole=[(820, 760), (880, 760), (880, 880), (820, 880)], faces=face_side)),
        "front": dict(args=("front", [(640, 790), (703, 792), (712, 880), (700, 960), (665, 1010), (600, 1110), (545, 1245), (465, 1245),
                                      (495, 1080), (555, 960), (598, 880)], (680, 815)), kw=dict(sym=True, faces=face_front)),
        "back": dict(args=("back", [(836, 784), (882, 788), (908, 878), (930, 928), (1000, 1172), (978, 1194), (900, 1012), (858, 952), (836, 880)], (858, 806)),
                     kw=dict(sym=True, head_line=712)),
    }
    only = sys.argv[1:]
    seqs = {}
    for name, c in CFG.items():
        rig = Rig2(*c["args"], **c["kw"])
        head = 1 if name == "back" else 0
        for anim, keys in (("attack", ATTACK[name]), ("skill", SKILL[name]), ("hurt", HURT[name]), ("idle", idle_keys(head))):
            if only and anim not in only: continue
            seqs[(name, anim)] = render(rig, keys, os.path.join(OUT, f"player_{name}_{anim}.png"))

    for anim in ("attack", "idle", "skill", "hurt"):
        if (("side", anim)) not in seqs: continue
        T = 150
        strip = Image.new("RGBA", (T * N, T * 3), (58, 54, 70, 255))
        for k, name in enumerate(("side", "front", "back")):
            for i, f in enumerate(seqs[(name, anim)]):
                strip.alpha_composite(f.resize((T, T), Image.LANCZOS), (T * i, T * k))
        strip.convert("RGB").save(os.path.join(HERE, f"p16_{anim}.png"))
        gif = []
        for i in range(N):
            cimg = Image.new("RGBA", (300 * 3, 300), (58, 54, 70, 255))
            for k, name in enumerate(("side", "front", "back")):
                cimg.alpha_composite(seqs[(name, anim)][i].resize((300, 300), Image.LANCZOS), (300 * k, 0))
            gif.append(cimg.convert("RGB"))
        dur = {"attack": [30, 35, 40, 45, 50, 60, 70, 25, 20, 20, 25, 40, 60, 50, 45, 60],
               "idle": 100,
               "skill": [40, 45, 50, 40, 35, 35, 40, 45, 60, 80, 90, 90, 60, 50, 45, 60],
               "hurt": [25, 30, 35, 40, 40, 40, 35, 35, 35, 35, 35, 35, 35, 40, 50, 80]}[anim]
        gif[0].save(os.path.join(HERE, f"p16_{anim}.gif"), save_all=True, append_images=gif[1:], duration=dur, loop=0)
