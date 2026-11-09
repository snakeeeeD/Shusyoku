# 正面の攻撃：構えは AI に描いてもらった絵（work/front_ready.png）を使う。
# 0〜2 元の正面で沈む → 3〜6 構えの絵（6で止まる）→ 7〜8 少し引く → 9〜12 構えの絵の腕と剣を肩から振り下ろす → 13〜15 元の正面へ戻る
import math, os, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sprites as M
import make_sprites2 as S2
import make_sprites3 as S3
import make_sprites4 as S4
from make_sprites import CW, X, Y, poly, dil, rot_pt, stretch

HERE = S2.HERE


class ReadyRig:
    """構えの絵：体（腕を外して穴を埋めた）＋腕と剣（肩を軸に回す）"""
    def __init__(self, path, arm_poly, shoulder, hole_from_y):
        base = np.asarray(Image.open(path).convert("RGBA")).copy()
        al = base[..., 3] > 16
        arm = poly(arm_poly) & al
        arm = dil(arm, 2) & al
        hole = arm & (Y >= hole_from_y)                                    # 腕が体（マント・上着）に重なっていた所
        body = base.copy(); body[arm & ~hole] = 0
        body = M.inpaint(body, hole, n=120)                                # 周りの色で埋める（輪郭の黒からは広げない）
        self.BODY = Image.fromarray(body, "RGBA")
        self.ARM = M.layer(base, arm)
        self.SH = shoulder
        ay = np.nonzero(al)[0]
        self.FEET = ay.max(); self.HIP = (768, ay.max() - 60)

    def frame(self, aa=0.0, dy=0.0, sy=1.0, ghost=None):
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0))
        f.alpha_composite(self.BODY, (0, int(dy)))
        if ghost is not None:
            for k, tt in enumerate((0.2, 0.4, 0.6, 0.8)):
                g = self.ARM.rotate(ghost + (aa - ghost) * tt, resample=Image.BICUBIC, center=self.SH)
                a2 = np.asarray(g).copy(); a2[..., 3] = (a2[..., 3] * (0.12 + 0.06 * k)).astype(np.uint8)
                f.alpha_composite(Image.fromarray(a2, "RGBA"), (0, int(dy)))
        f.alpha_composite(self.ARM.rotate(aa, resample=Image.BICUBIC, center=self.SH), (0, int(dy)))
        return stretch(f, sy, self.FEET)


def blend(a, b, t):
    """a→b を t(0..1) で混ぜる（元の正面へ戻るつなぎ）"""
    return Image.blend(a, b, t)


if __name__ == "__main__":
    OUT = os.path.join(HERE, "out16c"); os.makedirs(OUT, exist_ok=True)
    FRONT_ARM = [(622, 788), (690, 790), (688, 830), (670, 852), (656, 900), (660, 940), (665, 1010), (600, 1110), (545, 1245),
                 (465, 1245), (495, 1080), (556, 950), (574, 880), (598, 830)]
    base = S4.Rig4("front", FRONT_ARM, (680, 815), sym=True, faces=M.face_front, parts=S3.FRONT_PARTS)
    ready = ReadyRig(os.path.join(HERE, "work", "front_ready.png"),
                     [(560, 330), (670, 330), (664, 648), (702, 652), (704, 712), (694, 768), (708, 790), (702, 832),
                      (640, 848), (582, 842), (582, 790), (600, 760), (608, 708), (578, 702), (572, 650)],
                     (700, 792), hole_from_y=758)

    def B(**k):   # 元の正面のコマ
        p = dict(S2.DEF); p.update(k); p["_i"] = 0
        return base.frame2(0.0, p)

    idle = B()
    frames = [
        idle,
        B(aa=-8, sy=0.97, dy=6, arm2_r=6),                     # 沈み込み（予備動作）
        B(aa=-14, sy=0.93, dy=10, arm2_r=10),
        ready.frame(sy=1.04, dy=-6),                           # 構えに入る（少し伸び上がる）
        ready.frame(sy=0.99),
        ready.frame(sy=1.0),
        ready.frame(sy=1.0),                                   # 6：構え（ここで止まる）
        ready.frame(aa=10, sy=0.98, dy=-2),                    # 少し引いてためる
        ready.frame(aa=18, sy=0.96, dy=-4),
    ]
    # 9〜15：振り下ろしと戻りは、元の正面の絵で作った動き（肘が伸びる振り下ろしは回転では作れないため）。
    # 9 コマ目の残像は「剣を上に構えた位置」から出る（S4.FRONT_ATTACK の 8 コマ目）
    ps = S2.expand(S4.FRONT_ATTACK)
    for i in range(9, 16):
        q, p = ps[i - 1], ps[i]
        g = None
        if abs(q["aa"] - p["aa"]) >= 14 or abs(q["ws"] - p["ws"]) >= 14:
            g = (q["aa"], q["ls"], q["adx"], q["ady"], q["ws"])
        frames.append(base.frame2(i / S2.N, p, g))
    assert len(frames) == 16
    FC = S2.FC
    sh = Image.new("RGBA", (FC * 4, FC * 4), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.alpha_composite(f.resize((FC, FC), Image.LANCZOS), (FC * (i % 4), FC * (i // 4)))
    sh.save(os.path.join(OUT, "player_front_attack.png")); print("saved front attack")
    T = 200
    strip = Image.new("RGBA", (T * 16, T), (58, 54, 70, 255))
    for i, f in enumerate(frames): strip.alpha_composite(f.resize((T, T), Image.LANCZOS), (T * i, 0))
    strip.convert("RGB").save(os.path.join(HERE, "front_attack_ai.png"))
    dur = [30, 35, 40, 45, 50, 60, 300, 25, 20, 20, 25, 40, 60, 50, 45, 60]
    gif = [f.resize((360, 360), Image.LANCZOS) for f in frames]
    out = []
    for g in gif:
        c = Image.new("RGBA", (360, 360), (58, 54, 70, 255)); c.alpha_composite(g); out.append(c.convert("RGB"))
    out[0].save(os.path.join(HERE, "front_attack_ai.gif"), save_all=True, append_images=out[1:], duration=[d * 2 for d in dur], loop=0)
