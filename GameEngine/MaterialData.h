#pragma once
#include <string>
#include <map>
#include <vector>

struct BaseDef {
    std::string id, name, desc, type, rangeType, mainType;
    int range = 0, cost = 0, mainValue = 0;
    int modSlots = 2;
    int buyPrice = 0, sellPrice = 0;
    int layer = 0;   // 0=全層 / 1,2,3=その層だけ
    std::string vfx;   // 基本エフェクト名（effects.json）
};

struct MatEntry {
    std::string slot;                 // amplifyMain / sub / onHit / main / onArrival / none
    std::string type, buffType, trapType;
    int value = 0, duration = 0;
    std::string vfx;
    bool valid = false;
};

struct MaterialDef {
    std::string id, name, desc, tag;
    int cost = 0;
    std::map<std::string, MatEntry> entries;   // "Attack"/"Skill"/"Move"/"Power"/"all"
    int buyPrice = 0, sellPrice = 0;
    int layer = 0;   // 0=全層 / 1,2,3=その層だけ
    std::string vfx;

    const MatEntry* entryFor(const std::string& baseType) const {
        auto it = entries.find(baseType);
        if (it != entries.end()) return &it->second;
        it = entries.find("all");
        if (it != entries.end()) return &it->second;
        return nullptr;
    }
};