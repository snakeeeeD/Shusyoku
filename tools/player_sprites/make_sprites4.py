# 手首（剣だけ回す）を足した版。正面・背面の「構え」で、腕は胸の高さまで・剣は手首から立てる。
# make_sprites3 の Rig3 に、剣（刃＋つば）を腕から切り離して握りを軸に回す ws（手首の角度）を足す。
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sprites as M
import make_sprites2 as S2
import make_sprites3 as S3
from make_sprites import CW, X, Y, poly, dil, rot_pt, stretch, add_glow, add_sparks

S2.DEF.update(ws=0.0, under=0)


class Rig4(S3.Rig3):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        base = np.asarray(self.base).astype(int)
        r, g, b = base[..., 0], base[..., 1], base[..., 2]
        blade = self.arm & (b > 150) & (b > r + 40)
        ys, xs = np.nonzero(blade)
        d2 = (xs - self.SH[0]) ** 2 + (ys - self.SH[1]) ** 2
        bx, by = int(xs[d2.argmin()]), int(ys[d2.argmin()])          # 刃の付け根
        tx, ty = int(xs[d2.argmax()]), int(ys[d2.argmax()])          # 剣先
        L = math.hypot(tx - bx, ty - by); ux, uy = (tx - bx) / L, (ty - by) / L
        along = (X - bx) * ux + (Y - by) * uy                         # 剣の向きに沿った位置（付け根=0）
        perp = np.abs((X - bx) * uy - (Y - by) * ux)                  # 剣の線からの距離
        sword = self.arm & (along >= -26) & (along <= L + 40) & (perp <= 52)   # 剣の向きに沿った帯で切る（色に頼らない）
        self.grip = (int(round(bx - ux * 40)), int(round(by - uy * 40)))      # 付け根より少し手前＝握った手の中（手首の軸）
        arr = np.asarray(self.base)
        extra = self.arm & ~sword & (along >= -26)                   # 付け根より先の剣以外（腕の範囲に入った上着のすそ）は体に戻す
        armu = self.arm & ~sword & ~extra
        self.SWORD = M.layer(arr, sword)
        self.ARMU = M.layer(arr, armu)
        if extra.any():
            def give(lay):
                a2 = np.asarray(lay).copy(); a2[extra] = arr[extra]; return Image.fromarray(a2, "RGBA")
            self.BODY = give(self.BODY)
            self.ALT = {kk: give(v) for kk, v in self.ALT.items()}

    def put_arm3(self, f, aa, ab, dx, dy, ws=0.0, ls=1.0, alpha=1.0, adx=0.0, ady=0.0):
        if abs(ws) < 0.5:
            return self.put_arm2(f, aa, ab, dx, dy, ls, alpha=alpha, adx=adx, ady=ady)
        sh2 = rot_pt(self.SH, self.HIP, ab)
        ox, oy = sh2[0] - self.SH[0] + dx + adx, sh2[1] - self.SH[1] + dy + ady
        g1 = rot_pt(self.grip, self.SH, aa + ab)
        sw = self.SWORD.rotate(aa + ab + ws, resample=Image.BICUBIC, center=self.grip)
        am = self.ARMU.rotate(aa + ab, resample=Image.BICUBIC, center=self.SH)
        if alpha < 1.0:
            out = []
            for lay in (sw, am):
                a2 = np.asarray(lay).copy(); a2[..., 3] = (a2[..., 3] * alpha).astype(np.uint8); out.append(Image.fromarray(a2, "RGBA"))
            sw, am = out
        f.alpha_composite(sw, (int(round(g1[0] - self.grip[0] + ox)), int(round(g1[1] - self.grip[1] + oy))))
        f.alpha_composite(am, (int(round(ox)), int(round(oy))))      # 手（握り）は剣の上

    def frame2(self, t, p, ghost=None):
        aa, ab, dx, dy, sy, ls, ws = p["aa"], p["ab"], p["dx"], p["dy"], p["sy"], p["ls"], p["ws"]
        adx, ady = p["adx"], p["ady"]
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        under = p.get("under", 0) > 0.5                          # 腕をマントの下に描く（背面の構え：マントに隠れていた根元を隠す）
        if under: self.put_arm3(f, aa, ab, dx, dy, ws, ls, adx=adx, ady=ady)
        cape = M.wave(self.CAPE, t, p["amp"], self.top, self.bot, self.cx, self.sym)
        f.alpha_composite(cape.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        for name, pt in self.parts.items():
            if not pt["front"]: self.put_part(f, name, p, ab, dx, dy)
        behind = p["front"] < 0.5
        if behind and not under: self.put_arm3(f, aa, ab, dx, dy, ws, ls, adx=adx, ady=ady)
        face = p.get("face")
        body = self.ALT.get(face, self.BODY) if face else self.BODY
        f.alpha_composite(body.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        for name, pt in self.parts.items():
            if pt["front"]: self.put_part(f, name, p, ab, dx, dy)
        if ghost is not None:
            gaa, gls, gdx, gdy, gws = ghost
            for k, tt in enumerate((0.2, 0.4, 0.6, 0.8)):
                self.put_arm3(f, gaa + (aa - gaa) * tt, ab, dx, dy, gws + (ws - gws) * tt, gls + (ls - gls) * tt,
                              alpha=0.12 + 0.06 * k, adx=gdx + (adx - gdx) * tt, ady=gdy + (ady - gdy) * tt)
        if not behind and not under: self.put_arm3(f, aa, ab, dx, dy, ws, ls, adx=adx, ady=ady)
        if self.HEAD is not None and p.get("head", 0) > 0.5:
            f.alpha_composite(self.HEAD.rotate(ab, resample=Image.BICUBIC, center=self.HIP), (int(dx), int(dy)))
        f = stretch(f, sy, self.FEET)
        if p.get("glow", 0) > 0.01:
            f = add_sparks(add_glow(f, p["glow"]), p["_i"] * 0.5, p["glow"], self.center)
        return f


def render(rig, keys, path, ghost_min=14.0):
    ps = S2.expand(keys)
    frames = []
    for i, p in enumerate(ps):
        g = None
        if i > 0:
            q = ps[i - 1]
            if (abs(q["aa"] - p["aa"]) >= ghost_min or abs(q["ws"] - p["ws"]) >= ghost_min
                    or abs(q["ls"] - p["ls"]) >= 0.25 or abs(q["ady"] - p["ady"]) >= 40):
                g = (q["aa"], q["ls"], q["adx"], q["ady"], q["ws"])
        frames.append(rig.frame2(i / S2.N, p, g))
    sh = Image.new("RGBA", (S2.FC * 4, S2.FC * 4), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.alpha_composite(f.resize((S2.FC, S2.FC), Image.LANCZOS), (S2.FC * (i % 4), S2.FC * (i // 4)))
    sh.save(path); print("saved", os.path.basename(path))
    return frames


# 構え（6コマ目で止まる）は、縮みも傾きも戻してまっすぐ立つ。剣は手首から立てる
FRONT_ATTACK = [(0, {}),
                (3, dict(aa=-35, ws=-45, ab=-3, dy=-8, sy=0.96, amp=16, arm2_r=12)),
                (5, dict(aa=-60, ws=-95, ab=-2, dy=-6, sy=0.99, amp=14, arm2_r=20, legL_x=-8, legR_x=8)),
                (6, dict(aa=-58, ws=-100, ab=-1, dy=-4, sy=1.0, amp=13, arm2_r=18, legL_x=-8, legR_x=8)),   # 構え（顔はふつう。長く止まるので）（ここで止まる）
                (8, dict(aa=-48, ws=-95, ab=-3, dy=-10, sy=0.97, amp=18, arm2_r=24, legL_x=-8, legR_x=8, face="strain")),
                (10, dict(aa=62, ws=0, ab=8, dy=60, sy=0.95, amp=34, face="strain", arm2_r=-28, arm2_y=-6,
                          legL_y=52, legL_x=-22, legL_r=-6, legR_y=-58, legR_x=8, legR_r=9)),
                (12, dict(aa=55, ws=0, ab=6, dy=55, sy=0.98, amp=26, face="strain", arm2_r=-20,
                          legL_y=48, legL_x=-20, legL_r=-5, legR_y=-52, legR_x=6, legR_r=8)),
                (15, dict(aa=0, ab=0, dy=0, sy=1.0, amp=14))]
BACK_ATTACK = [(0, dict(head=1, under=1)),
               (3, dict(aa=35, ws=45, ab=3, dy=8, sy=0.96, amp=16, head=1, under=1)),
               (5, dict(aa=60, ws=95, ab=2, dy=6, sy=0.99, amp=14, head=1, under=1)),
               (6, dict(aa=58, ws=100, ab=1, dy=4, sy=1.0, amp=13, head=1, under=1)),                          # 構え（ここで止まる）
               (8, dict(aa=120, ws=40, ab=-2, dy=-30, sy=1.04, ady=-120, adx=-10, amp=28, head=1, front=0)),
               (10, dict(aa=185, ws=0, ab=-4, dy=-80, sy=0.96, ls=0.9, ady=-270, adx=-70, amp=34, head=1, front=0)),
               (12, dict(aa=182, ws=0, ab=-3, dy=-72, sy=0.98, ls=0.9, ady=-255, adx=-65, amp=26, head=1, front=0)),
               (13, dict(aa=140, ab=-1, dy=-40, sy=1.0, ls=1.0, ady=-80, amp=18, head=1)),
               (15, dict(aa=0, ab=0, dy=0, sy=1.0, ls=1.0, amp=14, head=1))]

if __name__ == "__main__":
    OUT = os.path.join(S2.HERE, "out16c"); os.makedirs(OUT, exist_ok=True)
    FRONT_ARM = [(622, 788), (690, 790), (688, 830), (670, 852), (656, 900), (660, 940), (665, 1010), (600, 1110), (545, 1245),
                 (465, 1245), (495, 1080), (556, 950), (574, 880), (598, 830)]   # 袖・手袋・剣だけ（腰の袋は入れない）
    front = Rig4("front", FRONT_ARM, (680, 815), sym=True, faces=M.face_front, parts=S3.FRONT_PARTS)
    back = Rig4("back", [(836, 784), (882, 788), (908, 878), (930, 928), (1000, 1172), (978, 1194), (900, 1012), (858, 952), (836, 880)],
                (858, 806), sym=True, head_line=712)
    print("grip front", front.grip, "back", back.grip)
    fs = render(front, FRONT_ATTACK, os.path.join(OUT, "player_front_attack.png"))
    for anim, keys in (("skill", S3.FRONT_SKILL), ("hurt", S3.FRONT_HURT), ("idle", S3.front_idle())):
        render(front, keys, os.path.join(OUT, f"player_front_{anim}.png"))
    bs = render(back, BACK_ATTACK, os.path.join(OUT, "player_back_attack.png"))
    T = 200
    strip = Image.new("RGBA", (T * 16, T * 2), (58, 54, 70, 255))
    for k, seq in enumerate((fs, bs)):
        for i, f in enumerate(seq): strip.alpha_composite(f.resize((T, T), Image.LANCZOS), (T * i, T * k))
    strip.convert("RGB").save(os.path.join(S2.HERE, "ready_new.png"))
    big = Image.new("RGBA", (384 * 2, 384), (58, 54, 70, 255))
    big.alpha_composite(fs[6].resize((384, 384), Image.LANCZOS), (0, 0)); big.alpha_composite(bs[6].resize((384, 384), Image.LANCZOS), (384, 0))
    big.save(os.path.join(S2.HERE, "ready_hold.png"))
