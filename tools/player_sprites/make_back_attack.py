# 背面の攻撃：構えは AI に描いてもらった絵（work/back_ready.png）。正面（make_front_attack.py）と同じ組み方。
# 0〜2 元の背面で沈む → 3〜6 構えの絵（6で止まる）→ 7〜8 ためる → 9〜15 元の背面の振り下ろし（頭の向こうで振る）と戻り
import os, sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_sprites as M
import make_sprites2 as S2
import make_sprites4 as S4
from make_sprites import CW, stretch
import numpy as np

HERE = S2.HERE
if __name__ == "__main__":
    OUT = os.path.join(HERE, "out16c"); os.makedirs(OUT, exist_ok=True)
    base = S4.Rig4("back", [(836, 784), (882, 788), (908, 878), (930, 928), (1000, 1172), (978, 1194), (900, 1012), (858, 952), (836, 880)],
                   (858, 806), sym=True, head_line=712)
    ready = Image.open(os.path.join(HERE, "work", "back_ready.png")).convert("RGBA")
    feet = np.nonzero(np.asarray(ready)[..., 3] > 16)[0].max()

    def R(sy=1.0, dy=0):
        f = Image.new("RGBA", (CW, CW), (0, 0, 0, 0)); f.alpha_composite(ready, (0, int(dy)))
        return stretch(f, sy, feet)

    def B(**k):
        p = dict(S2.DEF); p.update(k); p["_i"] = 0
        return base.frame2(0.0, p)

    frames = [B(head=1, under=1), B(aa=8, sy=0.97, dy=6, head=1, under=1), B(aa=14, sy=0.93, dy=10, head=1, under=1),
              R(1.04, -6), R(0.99), R(1.0), R(1.0),           # 3〜6：構え（6で止まる）
              R(0.98, -2), R(0.96, -4)]                       # 7〜8：ためる
    ps = S2.expand(S4.BACK_ATTACK)
    for i in range(9, 16):                                    # 9〜15：振り下ろしと戻り（9 の残像は剣を上に構えた位置から）
        q, p = ps[i - 1], ps[i]
        g = None
        if abs(q["aa"] - p["aa"]) >= 14 or abs(q["ws"] - p["ws"]) >= 14 or abs(q["ady"] - p["ady"]) >= 40:
            g = (q["aa"], q["ls"], q["adx"], q["ady"], q["ws"])
        frames.append(base.frame2(i / S2.N, p, g))
    FC = S2.FC
    sh = Image.new("RGBA", (FC * 4, FC * 4), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sh.alpha_composite(f.resize((FC, FC), Image.LANCZOS), (FC * (i % 4), FC * (i // 4)))
    sh.save(os.path.join(OUT, "player_back_attack.png")); print("saved back attack")
