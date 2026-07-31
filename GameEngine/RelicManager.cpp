#include "RelicManager.h"
#include "PlayerDataManager.h"

#include <fstream>
#include <algorithm>
#include <windows.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

std::unordered_map<std::string, RelicDef> RelicManager::s_defs;

void RelicManager::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) { OutputDebugStringA("[Relic] file not found\n"); return; }
    try {
        json j; f >> j;
        for (auto& r : j["relics"]) {
            RelicDef d;
            d.id = r["id"]; d.name = r.value("name", "");
            d.desc = r.value("desc", ""); d.kind = r.value("kind", "");
            d.value = r.value("value", 0);
            s_defs[d.id] = d;
        }
        char buf[64]; sprintf_s(buf, "[Relic] loaded %d\n", (int)s_defs.size());
        OutputDebugStringA(buf);
    }
    catch (const std::exception& e) {
        OutputDebugStringA("[Relic] parse error: "); OutputDebugStringA(e.what()); OutputDebugStringA("\n");
    }
}

const RelicDef* RelicManager::Get(const std::string& id) {
    auto it = s_defs.find(id); return it != s_defs.end() ? &it->second : nullptr;
}

bool RelicManager::Owns(const std::string& id) {
    auto& v = PlayerDataManager::GetData().relics;
    return std::find(v.begin(), v.end(), id) != v.end();
}

int RelicManager::SumValue(const std::string& kind) {
    int sum = 0;
    for (auto& id : PlayerDataManager::GetData().relics)
        if (auto d = Get(id)) if (d->kind == kind) sum += d->value;
    return sum;
}

bool RelicManager::HasKind(const std::string& kind) {
    for (auto& id : PlayerDataManager::GetData().relics)
        if (auto d = Get(id)) if (d->kind == kind) return true;
    return false;
}