#include "RelicManager.h"
#include "PlayerDataManager.h"

#include <fstream>
#include <algorithm>
#include <windows.h>
#include <vector>
#include <cstdlib>

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
            d.rarity = r.value("rarity", "common");
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

std::string RelicManager::RandomUnowned(const std::string& tier)
{
    std::vector<std::string> pool;
    for (auto& kv : s_defs)
        if (!Owns(kv.first) && (tier.empty() || kv.second.rarity == tier))
            pool.push_back(kv.first);
    if (pool.empty()) return "";
    return pool[rand() % pool.size()];
}

std::string RelicManager::RandomDrop()   // 通常/エリート用：common/uncommon/rare を重み付け
{
    struct W { const char* t; int w; }; W tiers[] = { {"common",65},{"uncommon",27},{"rare",8} };
    std::vector<std::pair<std::string, int>> pool;
    for (auto& kv : s_defs)
    {
        if (Owns(kv.first)) continue;
        int w = 0; for (auto& t : tiers) if (kv.second.rarity == t.t) w = t.w;
        if (w > 0) pool.push_back({ kv.first, w });
    }
    if (pool.empty()) return "";
    int total = 0; for (auto& p : pool) total += p.second;
    int roll = rand() % total, acc = 0;
    for (auto& p : pool) { acc += p.second; if (roll < acc) return p.first; }
    return pool.back().first;
}

std::vector<std::string> RelicManager::ShopPool()
{
    std::vector<std::string> pool;
    for (auto& kv : s_defs)
    {
        if (Owns(kv.first)) continue;
        const std::string& r = kv.second.rarity;
        if (r == "boss" || r == "event") continue;   // ショップでは売らない
        pool.push_back(kv.first);
    }
    return pool;
}

std::vector<std::string> RelicManager::AllIds()
{
    std::vector<std::string> v;
    for (auto& kv : s_defs) v.push_back(kv.first);
    return v;
}