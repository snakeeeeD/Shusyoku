import subprocess, os, sys
REC = "C:\\Users\\masam\\OneDrive\\デスクトップ\\Syusyoku\\GameEngine\\Recordings\\"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "clips_out")
os.makedirs(OUT, exist_ok=True)
CROP = "crop=1220:686:315:70,scale=640:360:flags=lanczos,setsar=1"

CLIPS = {
    "gloss_intent":         ("093413", [(2.0, 9.4)]),
    "gloss_sanctuary":      ("093607", [(2.0, 5.0), (12.5, 19.8)]),
    "gloss_trap":           ("100508", [(0.5, 4.2)]),
    "gloss_trap_explosion": ("100508", [(15.0, 19.5)]),
    "gloss_trap_root":      ("100642", [(1.5, 5.6), (8.3, 14.6)]),
    "gloss_trap_poison":    ("100745", [(2.5, 11.3)]),
    "gloss_trap_weaken":    ("100845", [(1.5, 10.0)]),
    "gloss_trap_expose":    ("100921", [(1.5, 7.8)]),
    "gloss_faith":          ("101540", [(1.5, 9.0)]),
    "gloss_engrave":        ("101822", [(0.5, 5.5)]),
    "gloss_holy_ground":    ("101822", [(8.0, 15.5)]),
    "gloss_knockback":      ("103349", [(24.8, 28.8)]),
    "gloss_pull":           ("103131", [(8.4, 13.4), (27.2, 30.0), (38.4, 41.0)]),
    "gloss_threat_area":    (None, [("103548", 0.0, 3.8), ("103548", 8.5, 15.0, "crop=1100:619:435:230,scale=640:360:flags=lanczos,setsar=1")]),   # 後半はカメラが引いている
    "gloss_floor_fire":     ("103735", [(2.0, 8.0)]),
    "gloss_burn":           ("103735", [(7.5, 15.5)]),
    "gloss_floor_thorn":    ("103850", [(4.0, 12.5)]),
    "gloss_floor_swamp":    ("103921", [(2.0, 14.5)]),
    "gloss_floor_ice":      ("104116", [(1.0, 3.5), (13.5, 22.5)]),
    "gloss_floor":          (None, [("103735", 2.0, 5.5), ("103850", 5.0, 8.5)]),   # 別の録画をつなぐ
    "gloss_dash":           (None, [("104506", 9.5, 16.8, "crop=1300:731:240:60,scale=640:360:flags=lanczos,setsar=1")]),
    "gloss_summon":         (None, [("104552", 1.0, 7.8, "crop=1067:600:470:100,scale=640:360:flags=lanczos,setsar=1")]),
    "gloss_big_enemy":      (None, [("104759", 0.5, 10.5, "crop=1067:600:430:120,scale=640:360:flags=lanczos,setsar=1")]),
    "gloss_immovable":      ("112621", [(3.0, 12.6)]),   # 撮り直し（111228 から差し替え）
    "gloss_weak_point":     (None, [("111909", 5.0, 9.8, "crop=1160:652:380:170,scale=640:360:flags=lanczos,setsar=1")]),
    "gloss_molt":           (None, [("112148", 10.0, 16.5, "crop=1180:664:360:120,scale=640:360:flags=lanczos,setsar=1")]),
}
only = sys.argv[1:]
for name, (rec, segs) in CLIPS.items():
    if only and name not in only: continue
    segs = [(tuple(sg) if len(sg) >= 3 and isinstance(sg[0], str) else (rec,) + tuple(sg)) for sg in segs]
    out = os.path.join(OUT, name + ".mpg")
    if len(segs) == 1:
        rec = segs[0][0]; crop1 = segs[0][3] if len(segs[0]) > 3 else CROP; segs = [segs[0][1:3]]
        src = REC + "rec_20261109_" + rec + ".mpg"
        s, e = segs[0]
        cmd = ["ffmpeg", "-v", "error", "-y", "-ss", str(s), "-i", src, "-t", str(e - s), "-vf", crop1,
               "-c:v", "mpeg1video", "-q:v", "3", "-an", out]
    else:
        ins, fc = [], []
        for k, sg in enumerate(segs):
            r, s, e = sg[:3]; crop = sg[3] if len(sg) > 3 else CROP          # 区間ごとに切り取りを変えられる
            ins += ["-ss", str(s), "-t", str(e - s), "-i", REC + "rec_20261109_" + r + ".mpg"]
            fc.append(f"[{k}:v]{crop},setpts=PTS-STARTPTS[v{k}]")
        fc.append("".join(f"[v{k}]" for k in range(len(segs))) + f"concat=n={len(segs)}:v=1:a=0[o]")
        cmd = ["ffmpeg", "-v", "error", "-y"] + ins + ["-filter_complex", ";".join(fc), "-map", "[o]",
               "-c:v", "mpeg1video", "-q:v", "3", "-an", out]
    subprocess.run(cmd, check=True)
    d = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", out],
                       capture_output=True, text=True).stdout.strip()
    print(name, d)
