# 手足も動かす版（まず正面）。make_sprites2 の Rig2 に「空いている腕」「左右の脚」を足す。
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sprites as M
import make_sprites2 as S2
from make_sprites import CW, X, Y, poly, dil, rot_pt, stretch, add_glow, add_sparks

S2.DEF.update(arm2_r=0.0, arm2_x=0.0, arm2_y=0.0, legL_r=0.0, legL_x=0.0, legL_y=0.0, legR_r=0.0, legR_x=0.0, legR_y=0.0)


class Rig3(S2.Rig2):
    """parts: {名前: dict(poly, pivot, front(体より手前か), hole(体側を上着の色で埋める範囲), extend_up(上へ伸ばすpx))}"""
    def __init__(self, *a, parts=None, **k):
        super().__init__(*a, **k)
        base = np.asarray(self.base).copy()
        al = base[..., 3] > 16
        red = dil(M.red_mask(base.astype(int)), 2)
        self.parts = {}
        body = np.asarray(self.BODY).copy()
        alts = {kk: np.asarray(v).copy() for kk, v in self.ALT.items()}
        for name, c in (parts or {}).items():
            m = poly(c["poly"]) & al & ~red & ~self.arm
            m = dil(m, 1) & al & ~red & ~self.arm
            arr = np.where(m[..., None], base, 0).astype(np.uint8)
            if c.get("extend_up"):                       # 脚：上の端の色を上へ伸ばす（下げた時にすその下が空かない）
                n = c["extend_up"]
                ys, xs = np.nonzero(m)
                hidden = (body[..., 3] > 0) & ~m                    # 上着などに隠れている所だけに伸ばす（横にはみ出さない）
                for x in np.unique(xs):
                    col = ys[xs == x]; top = col.min()
                    src = arr[min(top + 10, CW - 1), x]
                    if src[3] == 0 or int(src[:3].max()) < 60: continue   # 輪郭の黒は伸ばさない（縦すじになる）
                    for y in range(top - 1, max(0, top - n) - 1, -1):
                        if not hidden[y, x]: break
                        arr[y, x] = src
            lay = Image.fromarray(arr, "RGBA")
            for b in [body] + list(alts.values()):
                b[m] = 0
                if c.get("hole") is not None:            # 腕に隠れていた胴を上着の色で埋める
                    hole = m & poly(c["hole"])
                    tz = poly(c["hole"]) & ~m & (base[..., 3] > 200)
                    navy = tz & (base[..., 2] > base[..., 0] + 15) & (base[..., 2] > 60)
                    if navy.sum() > 30:
                        col = np.median(base[navy][:, :3], axis=0).astype(np.uint8)
                        b[hole] = np.append(col, 255)
            self.parts[name] = dict(layer=lay, pivot=c["pivot"], front=c.get("front", 1))
        self.BODY = Image.fromarray(body, "RGBA")
        self.ALT = {kk: Image.fromarray(v, "RGBA") for kk, v in alts.items()}

    def put_part(self, f, name, p, ab, dx, dy):
        pt = self.parts[name]; piv = pt["pivot"]
        r, ox, oy = p[name + "_r"], p[name + "_x"], p[name + "_y"]
        pv2 = rot_pt(piv, self.HIP, ab)
        lay = pt["layer"].rotate(r + ab, resample=Image.BICUBIC, center=piv)
        f.alpha_composite(lay, (int(round(pv2[0] - piv[0] + dx + ox)), int(round(pv2[1] - piv[1] + dy + oy))))

    def frame2(self, t, p, ghost=None):
        aa, ab, dx, dy, sy, ls = p["aa"], p["ab"], p["dx"], p["dy"], p["sy"], p["ls"]
        adx, ady = p["adx"], p["ady"]
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        cape = M.wave(self.CAPE, t, p["amp"], self.top, self.bot, self.cx, self.sym)
        f.alpha_composite(cape.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        for name, pt in self.parts.items():                       # 体より奥の部品（脚など）
            if not pt["front"]: self.put_part(f, name, p, ab, dx, dy)
        behind = p["front"] < 0.5
        if behind: self.put_arm2(f, aa, ab, dx, dy, ls, adx=adx, ady=ady)
        face = p.get("face")
        body = self.ALT.get(face, self.BODY) if face else self.BODY
        f.alpha_composite(body.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        for name, pt in self.parts.items():                       # 体より手前の部品（空いている腕）
            if pt["front"]: self.put_part(f, name, p, ab, dx, dy)
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


FRONT_PARTS = {
    "arm2": dict(poly=[(852, 805), (916, 805), (936, 858), (962, 878), (962, 978), (868, 978), (862, 900), (848, 850)],
                 pivot=(882, 815), front=1, hole=[(842, 805), (880, 805), (880, 905), (842, 905)]),
    "legL": dict(poly=[(585, 1042), (747, 1042), (747, 1245), (580, 1245)], pivot=(690, 1035), front=0, extend_up=70),
    "legR": dict(poly=[(803, 1042), (968, 1042), (968, 1245), (803, 1245)], pivot=(850, 1035), front=0, extend_up=70),
}

# 正面：剣の腕＋空いている腕（逆に振る）＋脚（踏み込み・踏ん張り）
FRONT_ATTACK = [(0, {}),
                (4, dict(aa=-125, ab=-6, dy=-24, sy=0.90, face="strain", amp=18, arm2_r=45, legL_y=-8, legR_y=4)),
                (6, dict(aa=-135, ab=-7, dy=-30, sy=0.88, face="strain", amp=18, arm2_r=58, legL_y=-10, legR_y=6)),
                (8, dict(aa=-30, ab=4, dy=30, sy=1.08, ls=1.15, face="strain", amp=30, arm2_r=-5,
                         legL_y=28, legL_x=-10, legR_y=-30, legR_r=4)),
                (10, dict(aa=62, ab=8, dy=60, sy=0.95, ls=1.2, face="strain", amp=34, arm2_r=-28, arm2_y=-6,
                          legL_y=52, legL_x=-22, legL_r=-6, legR_y=-58, legR_x=8, legR_r=9)),
                (12, dict(aa=55, ab=6, dy=55, sy=0.98, ls=1.15, face="strain", amp=26, arm2_r=-20,
                          legL_y=48, legL_x=-20, legL_r=-5, legR_y=-52, legR_x=6, legR_r=8)),
                (15, dict(aa=0, ab=0, dy=0, sy=1.0, ls=1.0, amp=14))]
FRONT_SKILL = [(0, {}),
               (3, dict(aa=15, ab=3, dy=30, sy=0.86, amp=16, glow=0.15, arm2_r=8, legL_x=-12, legR_x=12, legL_r=4, legR_r=-4)),
               (6, dict(aa=-90, ab=-3, dy=-50, sy=1.10, amp=32, glow=0.6, arm2_r=40, legL_y=14, legR_y=10, legL_r=-5, legR_r=5)),
               (9, dict(aa=-130, ab=-3, dy=-60, sy=1.08, amp=40, glow=1.0, arm2_r=55, legL_y=18, legR_y=14, legL_r=-7, legR_r=7)),
               (12, dict(aa=-130, ab=-3, dy=-55, sy=1.06, amp=36, glow=1.0, arm2_r=50, legL_y=12, legR_y=10, legL_r=-5, legR_r=5)),
               (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=14, glow=0.2))]
FRONT_HURT = [(0, {}),
              (2, dict(aa=-20, ab=-10, dy=-50, sy=0.86, face="pain", amp=34, arm2_r=70, legL_y=-20, legL_r=8, legR_y=36, legR_r=-6)),
              (5, dict(aa=-28, ab=10, dy=-70, sy=0.92, face="pain", amp=36, arm2_r=85, arm2_y=-8, legL_y=-30, legL_r=12, legR_y=50, legR_r=-8)),
              (9, dict(aa=-10, ab=-6, dy=-30, sy=1.02, face="pain", amp=22, arm2_r=30, legL_y=-8, legL_r=4, legR_y=22, legR_r=-3)),
              (12, dict(aa=4, ab=3, dy=-6, sy=1.0, amp=16, arm2_r=-6, legR_y=4)),
              (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=12))]


def front_idle():
    ks = []
    for i in range(S2.N):
        t = i / S2.N; b = math.sin(2 * math.pi * t)
        ks.append((i, dict(sy=1.0 + 0.010 * b, dy=-2 * b, ab=0.4 * math.sin(2 * math.pi * t + 0.8),   # 待機は控えめに
                           aa=1.5 * math.sin(2 * math.pi * t + 1.2), arm2_r=-1.5 * math.sin(2 * math.pi * t + 1.2),
                           legL_y=2 * b, legR_y=2 * b,                       # 体が上下しても足は地面に
                           amp=11 + 3 * (0.5 + 0.5 * math.sin(2 * math.pi * t + 1.0)))))
    return ks


if __name__ == "__main__":
    OUT = os.path.join(S2.HERE, "out16b"); os.makedirs(OUT, exist_ok=True)
    rig = Rig3("front", [(640, 790), (703, 792), (712, 880), (700, 960), (665, 1010), (600, 1110), (545, 1245), (465, 1245),
                         (495, 1080), (555, 960), (598, 880)], (680, 815), sym=True, faces=M.face_front, parts=FRONT_PARTS)
    seqs = {}
    for anim, keys in (("attack", FRONT_ATTACK), ("skill", FRONT_SKILL), ("hurt", FRONT_HURT), ("idle", front_idle())):
        seqs[anim] = S2.render(rig, keys, os.path.join(OUT, f"player_front_{anim}.png"))
    # 前（今のゲーム）と後（手足あり）を並べた GIF
    old = {}
    for anim in seqs:
        sh = Image.open(os.path.join(S2.OUT, f"player_front_{anim}.png"))
        old[anim] = [sh.crop((384 * (i % 4), 384 * (i // 4), 384 * (i % 4) + 384, 384 * (i // 4) + 384)) for i in range(16)]
    dur = {"attack": [30, 35, 40, 45, 50, 60, 70, 25, 20, 20, 25, 40, 60, 50, 45, 60], "idle": 100,
           "skill": [35, 40, 45, 35, 30, 30, 35, 40, 50, 60, 70, 60, 45, 40, 35, 50],
           "hurt": [25, 30, 35, 40, 40, 40, 35, 35, 35, 35, 35, 35, 35, 40, 50, 80]}
    frames = []; durs = []
    for anim in ("attack", "skill", "hurt", "idle"):
        d = dur[anim] if isinstance(dur[anim], list) else [dur[anim]] * 16
        for i in range(16):
            c = Image.new("RGBA", (640, 340), (58, 54, 70, 255))
            c.alpha_composite(old[anim][i].resize((320, 320), Image.LANCZOS), (0, 10))
            c.alpha_composite(seqs[anim][i].resize((320, 320), Image.LANCZOS), (320, 10))
            frames.append(c.convert("RGB")); durs.append(d[i] * 2)   # 見やすいよう2倍の遅さ
        frames.append(frames[-1]); durs.append(400)
    frames[0].save(os.path.join(S2.HERE, "front_limbs.gif"), save_all=True, append_images=frames[1:], duration=durs, loop=0)
    strip = Image.new("RGBA", (200 * 16, 200 * 4), (58, 54, 70, 255))
    for k, anim in enumerate(("attack", "skill", "hurt", "idle")):
        for i, f in enumerate(seqs[anim]): strip.alpha_composite(f.resize((200, 200), Image.LANCZOS), (200 * i, 200 * k))
    strip.convert("RGB").save(os.path.join(S2.HERE, "front_limbs.png"))
