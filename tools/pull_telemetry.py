#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
pull_telemetry.py

Supabaseの telemetry_sessions テーブルから、テスターが送ってきたセッションを取得し、
logs/remote/session_<id>.jsonl として保存する。
保存後は既存の watch_telemetry / telemetry_to_excel がそのまま拾って集計する。

秘密鍵(service_role / secret)の置き場所（どちらでも可・gitignore済）:
  - 環境変数 SUPABASE_SERVICE_KEY
  - ファイル tools/.supabase_service_key （中身は鍵だけ1行）

単体実行:
    python tools/pull_telemetry.py
"""
import os, sys, json, glob, urllib.request, urllib.parse, urllib.error

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
except Exception:
    pass

# --- 接続情報（URLとテーブルは公開情報なので直書き。秘密鍵だけ外出し） ---
SUPABASE_URL = "https://rjshabmjcrshzwsctesi.supabase.co"
TABLE = "telemetry_sessions"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
KEY_FILE = os.path.join(HERE, ".supabase_service_key")
STATE_FILE = os.path.join(HERE, ".pull_state.json")
OUT_DIR = os.path.join(ROOT, "logs", "remote")


def read_secret():
    k = os.environ.get("SUPABASE_SERVICE_KEY", "").strip()
    if k:
        return k
    if os.path.isfile(KEY_FILE):
        return open(KEY_FILE, encoding="utf-8").read().strip()
    return ""


def load_state():
    try:
        return json.load(open(STATE_FILE, encoding="utf-8"))
    except Exception:
        return {}


def save_state(s):
    try:
        json.dump(s, open(STATE_FILE, "w", encoding="utf-8"))
    except OSError:
        pass


def http_get_json(url, key):
    req = urllib.request.Request(url, headers={
        "apikey": key,
        "Authorization": "Bearer " + key,
        "Accept": "application/json",
    })
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode("utf-8"))


def local_session_ids():
    """logs/ 直下（このPC自身のプレイ）のsession id。remoteと二重に取り込まないため。"""
    ids = set()
    for p in glob.glob(os.path.join(ROOT, "logs", "session_*.jsonl")):
        b = os.path.basename(p)
        ids.add(b[len("session_"):-len(".jsonl")])
    return ids


def pull():
    """戻り値: (成功?, メッセージ)"""
    key = read_secret()
    if not key:
        return True, "秘密鍵が未設定のため取得スキップ（tools/.supabase_service_key）"

    state = load_state()
    since = state.get("last_created", "")
    q = {"select": "session_id,payload,created_at", "order": "created_at.asc"}
    if since:
        q["created_at"] = "gt." + since
    url = f"{SUPABASE_URL}/rest/v1/{TABLE}?" + urllib.parse.urlencode(q)

    try:
        rows = http_get_json(url, key)
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", "replace")[:200]
        return False, f"取得失敗 HTTP{e.code}: {body}"
    except Exception as e:
        return False, f"取得失敗: {e}"

    if not rows:
        return True, "新着なし"

    os.makedirs(OUT_DIR, exist_ok=True)
    skip_ids = local_session_ids()
    latest = {}                 # session_id -> (created_at, payload)
    max_created = since
    for row in rows:
        sid = str(row.get("session_id", "")).strip()
        ca = row.get("created_at", "") or ""
        if ca > max_created:
            max_created = ca
        if not sid or sid in skip_ids:
            continue
        cur = latest.get(sid)
        if cur is None or ca >= cur[0]:     # 同一sessionは最新を採用
            latest[sid] = (ca, row.get("payload", "") or "")

    written = 0
    for sid, (ca, pl) in latest.items():
        safe = "".join(ch for ch in sid if ch.isalnum() or ch in "._-")
        path = os.path.join(OUT_DIR, f"session_{safe}.jsonl")
        try:
            with open(path, "w", encoding="utf-8") as f:
                f.write(pl if pl.endswith("\n") else pl + "\n")
            written += 1
        except OSError:
            pass

    if max_created and max_created != since:
        state["last_created"] = max_created
        save_state(state)

    return True, f"取得 {written}件（session {len(latest)}種）"


def main():
    ok, msg = pull()
    print(("取得OK " if ok else "取得NG ") + msg)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
