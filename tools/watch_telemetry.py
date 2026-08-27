#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
watch_telemetry.py

logs/ を見張って、テレメトリのログが更新されるたびに自動で
telemetry_to_excel.py を実行し、Excelを最新化する常駐ツール。
さらに一定間隔で Supabase から他PC（テスター）のログを取得(pull)し、
logs/remote/ に落として同じ流れで集計する。

使い方（プロジェクト直下で）:
    python tools/watch_telemetry.py
    （起動したまま放置。自分のプレイも他PCのプレイも自動でExcelに反映される）
    止めるときは Ctrl+C

注意:
    Excelでレポートを開いたままだと上書きできない（Windowsの仕様）。
    その場合は「開いています」と表示して、閉じられ次第 自動で更新する。
"""
import os, sys, time, glob, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)     # プロジェクト直下
sys.path.insert(0, HERE)
import pull_telemetry            # 同じtools/内。Supabase取得を担当

# コンソールの文字コードに関係なくクラッシュしないように
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
except Exception:
    pass

POLL_SEC = 4                     # ローカルログの監視間隔
SETTLE_SEC = 2                   # 書き込みが落ち着くまで少し待つ
PULL_SEC = 60                    # Supabaseから他PCのログを取りに行く間隔
SCRIPT = os.path.join(HERE, "telemetry_to_excel.py")
OUT = os.path.join(HERE, "telemetry_report.xlsx")

SCAN_DIRS = ["logs", "x64/Debug/logs", "x64/Release/logs",
             "GameEngine/logs", "GameEngine/x64/Debug/logs"]


def latest_mtime():
    m = 0.0
    for d in SCAN_DIRS:
        # remote/ など入れ子も拾えるよう再帰
        for p in glob.glob(os.path.join(ROOT, d, "**", "session_*.jsonl"), recursive=True):
            try:
                m = max(m, os.path.getmtime(p))
            except OSError:
                pass
    return m


def regenerate():
    """集計スクリプトを実行。戻り値: (成功?, メッセージ)"""
    try:
        r = subprocess.run([sys.executable, SCRIPT, "-o", OUT],
                           cwd=ROOT, capture_output=True, text=True)
    except Exception as e:
        return False, f"実行エラー: {e}"
    out = (r.stdout or "").strip().splitlines()
    if r.returncode == 0:
        return True, (out[-1] if out else "更新しました")
    err = (r.stderr or "") + (r.stdout or "")
    if "Permission denied" in err or "PermissionError" in err:
        return False, "Excelでレポートを開いているため更新できません → 閉じてください（閉じ次第 自動更新）"
    return False, "更新に失敗: " + (err.strip().splitlines()[-1] if err.strip() else "不明なエラー")


def do_pull():
    """Supabaseから他PCのログを取得。取得できたらメッセージを表示。"""
    ok, msg = pull_telemetry.pull()
    if not ok or ("取得 " in msg and "取得 0件" not in msg):
        ts = time.strftime("%H:%M:%S")
        print(f"[{ts}] " + ("取得 " if ok else "取得NG ") + msg)


def main():
    print("=== テレメトリ自動更新を開始 ===")
    print(f"監視: {', '.join(SCAN_DIRS)} ＋ Supabase(他PC)")
    print(f"出力: {OUT}")
    print("自分のプレイも他PCのプレイも自動でExcelに反映されます（Ctrl+Cで停止）\n")

    last_seen = -1.0        # 最後に処理したログのmtime
    pending = None          # 変化を検知した時刻
    last_pull = 0.0         # 最後にSupabaseを見に行った時刻

    # 起動時に一度：他PC分を取得 → 集計
    do_pull()
    ok, msg = regenerate()
    print(("更新 " if ok else "待機 ") + msg)
    last_seen = latest_mtime()
    last_pull = time.time()

    try:
        while True:
            time.sleep(POLL_SEC)
            # 一定間隔でSupabaseから他PC分を取得（logs/remoteが増えればmtimeで検知される）
            if time.time() - last_pull >= PULL_SEC:
                do_pull()
                last_pull = time.time()
            m = latest_mtime()
            if m > last_seen:
                # 書き込み中かもしれないので、落ち着くまで待ってから
                if pending is None:
                    pending = m
                    continue
                if m != pending:            # まだ書き込みが続いている
                    pending = m
                    continue
                time.sleep(SETTLE_SEC)
                ok, msg = regenerate()
                ts = time.strftime("%H:%M:%S")
                print(f"[{ts}] " + ("更新 " if ok else "待機 ") + msg)
                if ok:
                    last_seen = m
                    pending = None
                # 失敗（Excel開きっぱ等）なら last_seen を更新せず、次周期で再試行
                else:
                    pending = None
    except KeyboardInterrupt:
        print("\n=== 停止しました ===")


if __name__ == "__main__":
    main()
