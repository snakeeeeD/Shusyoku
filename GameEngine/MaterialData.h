#pragma once
#include <string>
#include <map>
#include <vector>

struct BaseDef {
    std::string id, name, type, rangeType, mainType;
    int range = 0, cost = 0, mainValue = 0;
};

struct MatEntry {
    std::string slot;                 // amplifyMain / sub / onHit / main / onArrival / none
    std::string type, buffType, trapType;
    int value = 0, duration = 0;
    bool valid = false;
};

struct MaterialDef {
    std::string id, name;
    int cost = 0;
    std::map<std::string, MatEntry> entries;   // "Attack"/"Skill"/"Move"/"Power"/"all"

    const MatEntry* entryFor(const std::string& baseType) const {
        auto it = entries.find(baseType);
        if (it != entries.end()) return &it->second;
        it = entries.find("all");
        if (it != entries.end()) return &it->second;
        return nullptr;
    }
};