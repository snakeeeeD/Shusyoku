#pragma once
#include <vector>
#include <string>

struct RelicEffect { std::string kind; int value = 0; };   // 追加効果（マイナスで代償）

struct RelicDef {
    std::string id, name, desc, kind, rarity, effect;   // effect: "block" / "energy"
    int value = 0;
    int count = 0;
    bool perTurn = false;   // true=毎ターン0リセット / false=引き継ぎ

    std::vector<RelicEffect> extra;   // 追加効果（同じフックに合算。ボスレリックの代償など）

    // この効果の種類を持っているか（本体か追加効果のどちらか）
    bool Has(const std::string& k) const
    {
        if (kind == k) return true;
        for (auto& e : extra) if (e.kind == k) return true;
        return false;
    }
    // この効果の種類の値の合計（本体＋追加効果）
    int ValueOf(const std::string& k) const
    {
        int v = (kind == k) ? value : 0;
        for (auto& e : extra) if (e.kind == k) v += e.value;
        return v;
    }

    std::string icon;   // アイコンのテクスチャ名（Assets/UI/<icon>.png。空なら名前の文字で表示）
};