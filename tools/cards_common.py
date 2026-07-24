# -*- coding: utf-8 -*-
import os

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS_JSON = os.path.join(BASE, "GameEngine", "Assets", "Data", "cards.json")
CARDS_XLSX = os.path.join(BASE, "cards_edit.xlsx")

# 1列 = 1フィールド（往復で欠落しないよう全項目を持つ）
COLS = [
    "id", "name", "type", "rarity", "cost", "range", "rangeType",
    "exhaust", "pierce", "dash", "selfDamage", "vfx", "tags",
    "mainType", "mainValue", "mainDuration", "mainBuffType", "mainCardId", "mainTrapType",
    "subType", "subValue", "subDuration", "subBuffType",
    "onHitType", "onHitValue", "onHitDuration", "onHitBuffType",
    "up_mainValue", "up_subValue", "up_onHitValue", "up_cost", "up_range",
    "up_rangeType", "up_description",
    "description",
]

# 色分け（種別・レアリティ共通）
TYPE_FILL = {
    "Attack": "EA9999",   # 赤
    "Skill":  "9FC5E8",   # 青
    "Move":   "B6D7A8",   # 緑
    "Power":  "D5A6BD",   # 紫
}
RARITY_FILL = {
    "Common":   "EFEFEF",  # 灰
    "Uncommon": "A2D9D9",  # シアン
    "Rare":     "FFE599",  # 金
}

INT_COLS = {"cost", "range", "selfDamage",
            "mainValue", "mainDuration", "subValue", "subDuration",
            "onHitValue", "onHitDuration",
            "up_mainValue", "up_subValue", "up_onHitValue", "up_cost", "up_range"}
BOOL_COLS = {"exhaust", "pierce", "dash"}


def card_to_row(c):
    """cards.json の1カード -> 列名→値 の dict"""
    me = c.get("mainEffect", {}) or {}
    se = c.get("subEffect", {}) or {}
    oh = c.get("onHitEffect", {}) or {}
    up = c.get("upgrade", {}) or {}
    tags = c.get("tags", []) or []
    r = {k: None for k in COLS}
    r.update({
        "id": c.get("id"), "name": c.get("name"), "type": c.get("type"),
        "rarity": c.get("rarity"), "cost": c.get("cost"), "range": c.get("range"),
        "rangeType": c.get("rangeType"),
        "exhaust": c.get("exhaust"), "pierce": c.get("pierce"), "dash": c.get("dash"),
        "selfDamage": c.get("selfDamage"), "vfx": c.get("vfx"),
        "tags": ",".join(tags) if tags else None,
        "mainType": me.get("type"), "mainValue": me.get("value"),
        "mainDuration": me.get("duration"), "mainBuffType": me.get("buffType"),
        "mainCardId": me.get("cardId"), "mainTrapType": me.get("trapType"),
        "subType": se.get("type"), "subValue": se.get("value"),
        "subDuration": se.get("duration"), "subBuffType": se.get("buffType"),
        "onHitType": oh.get("type"), "onHitValue": oh.get("value"),
        "onHitDuration": oh.get("duration"), "onHitBuffType": oh.get("buffType"),
        "up_mainValue": up.get("mainValue"), "up_subValue": up.get("subValue"),
        "up_onHitValue": up.get("onHitValue"), "up_cost": up.get("cost"),
        "up_range": up.get("range"), "up_rangeType": up.get("rangeType"),
        "up_description": up.get("description"),
        "description": c.get("description"),
    })
    return r


def _to_int(v):
    if v is None or v == "":
        return None
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, float) and v.is_integer():
        return int(v)
    return int(v)


def _to_bool(v):
    if v is None or v == "":
        return None
    if isinstance(v, str):
        return v.strip().lower() in ("true", "1", "yes", "○", "x")
    return bool(v)


def row_to_card(row):
    """列名→値 の dict -> cards.json の1カード（Noneや空は省く）"""
    g = lambda k: row.get(k)
    if not g("id"):
        return None

    c = {"id": g("id"), "name": g("name"), "type": g("type")}
    if g("rarity"):
        c["rarity"] = g("rarity")
    if g("tags"):
        c["tags"] = [t.strip() for t in str(g("tags")).split(",") if t.strip()]
    c["cost"] = _to_int(g("cost")) or 0
    c["range"] = _to_int(g("range")) or 0
    c["rangeType"] = g("rangeType")
    c["description"] = g("description") or ""

    for k in ("exhaust", "pierce", "dash"):
        if _to_bool(g(k)):
            c[k] = True
    sd = _to_int(g("selfDamage"))
    if sd:
        c["selfDamage"] = sd
    if g("vfx"):
        c["vfx"] = g("vfx")

    # upgrade
    up = {}
    for src, key in (("up_mainValue", "mainValue"), ("up_subValue", "subValue"),
                     ("up_onHitValue", "onHitValue"), ("up_cost", "cost"),
                     ("up_range", "range")):
        iv = _to_int(g(src))
        if iv is not None:
            up[key] = iv
    if g("up_rangeType"):
        up["rangeType"] = g("up_rangeType")
    if g("up_description"):
        up["description"] = g("up_description")
    if up:
        c["upgrade"] = up

    # mainEffect（必須）
    me = {"type": g("mainType") or "None", "value": _to_int(g("mainValue")) or 0}
    if _to_int(g("mainDuration")) is not None:
        me["duration"] = _to_int(g("mainDuration"))
    if g("mainBuffType"):
        me["buffType"] = g("mainBuffType")
    if g("mainCardId"):
        me["cardId"] = g("mainCardId")
    if g("mainTrapType"):
        me["trapType"] = g("mainTrapType")
    c["mainEffect"] = me

    # subEffect / onHitEffect（type があれば）
    if g("subType"):
        se = {"type": g("subType"), "value": _to_int(g("subValue")) or 0}
        if _to_int(g("subDuration")) is not None:
            se["duration"] = _to_int(g("subDuration"))
        if g("subBuffType"):
            se["buffType"] = g("subBuffType")
        c["subEffect"] = se
    if g("onHitType"):
        oh = {"type": g("onHitType"), "value": _to_int(g("onHitValue")) or 0}
        if _to_int(g("onHitDuration")) is not None:
            oh["duration"] = _to_int(g("onHitDuration"))
        if g("onHitBuffType"):
            oh["buffType"] = g("onHitBuffType")
        c["onHitEffect"] = oh

    return c
