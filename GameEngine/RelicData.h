#pragma once
#include <string>

struct RelicDef {
    std::string id, name, desc, kind, rarity, effect;   // effect: "block" / "energy"
    int value = 0;
    int count = 0;
    bool perTurn = false;   // true=毎ターン0リセット / false=引き継ぎ
};