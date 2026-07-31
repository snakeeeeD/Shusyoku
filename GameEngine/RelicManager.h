#pragma once
#include "RelicData.h"
#include <string>
#include <unordered_map>

class RelicManager {
public:
    static void Load(const std::string& path);
    static const RelicDef* Get(const std::string& id);
    static bool Owns(const std::string& id);
    static int  SumValue(const std::string& kind);   // Š’†‚Åkindˆê’v‚Ìvalue‡Œv
    static bool HasKind(const std::string& kind);
    static std::string RandomUnowned(const std::string& tier = "");
    static std::string RandomDrop();
    static std::vector<std::string> ShopPool();
    static std::vector<std::string> AllIds();
private:
    static std::unordered_map<std::string, RelicDef> s_defs;
};