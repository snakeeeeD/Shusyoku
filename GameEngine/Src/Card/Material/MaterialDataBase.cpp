#include "MaterialDataBase.h"
#include <fstream>
#include <vector>
#include <cstdlib>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::unordered_map<std::string, BaseDef>     MaterialDataBase::s_bases;
std::unordered_map<std::string, MaterialDef> MaterialDataBase::s_materials;

static MatEntry parseEntry(const json& j) {
    MatEntry e;
    e.valid = true;
    e.slot = j.value("slot", "none");
    e.type = j.value("type", "");
    e.buffType = j.value("buffType", "");
    e.trapType = j.value("trapType", "");
    e.value = j.value("value", 0);
    e.duration = j.value("duration", 0);
    e.vfx = j.value("vfx", "");
    return e;
}

const BaseDef* MaterialDataBase::GetBase(const std::string& id) {
    auto it = s_bases.find(id);
    return it != s_bases.end() ? &it->second : nullptr;
}
const MaterialDef* MaterialDataBase::GetMaterial(const std::string& id) {
    auto it = s_materials.find(id);
    return it != s_materials.end() ? &it->second : nullptr;
}

std::string MaterialDataBase::RandomId(int layer) {
    std::vector<std::string> pool;
    for (auto& kv : s_materials) {
        const MaterialDef& m = kv.second;
        if (m.entries.empty()) continue;               // 効果なし(換金用など)は除外
        if (layer != 0 && m.layer != layer) continue;   // 層指定
        pool.push_back(kv.first);
    }
    if (pool.empty() && layer != 0)                      // 該当層が無ければ全体から
        for (auto& kv : s_materials)
            if (!kv.second.entries.empty()) pool.push_back(kv.first);
    if (pool.empty()) return "";
    return pool[rand() % pool.size()];
}

static BaseDef parseBase(const json& b) {
    BaseDef d;
    d.id = b["id"]; d.name = b.value("name", "");
    d.desc = b.value("desc", "");
    d.type = b["type"]; d.rangeType = b["rangeType"];
    d.range = b.value("range", 0); d.cost = b.value("cost", 0);
    d.mainType = b.value("mainType", "None"); d.mainValue = b.value("mainValue", 0);
    d.modSlots = b.value("modSlots", 2);
    d.buyPrice = b.value("buyPrice", 120);
    d.sellPrice = b.value("sellPrice", 60);
    d.layer = b.value("layer", 0);
    d.vfx = b.value("vfx", "");
    return d;
}

static MaterialDef parseMaterial(const json& m) {
    MaterialDef d;
    d.id = m["id"]; d.name = m.value("name", "");
    d.desc = m.value("desc", "");
    d.tag = m.value("tag", d.name);   // 未指定ならフル名にフォールバック
    d.cost = m.value("cost", 0);
    d.buyPrice = m.value("buyPrice", 40);
    d.sellPrice = m.value("sellPrice", 20);
    d.layer = m.value("layer", 0);
    for (const char* k : { "Attack", "Skill", "Move", "Power", "all" })
        if (m.contains(k)) d.entries[k] = parseEntry(m[k]);
    return d;
}

void MaterialDataBase::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    json j; f >> j;
    for (auto& b : j["bases"]) { BaseDef d = parseBase(b); s_bases[d.id] = d; }
    for (auto& m : j["materials"]) { MaterialDef d = parseMaterial(m); s_materials[d.id] = d; }
}

void MaterialDataBase::Reload(const std::string& path) {
    s_bases.clear();
    s_materials.clear();
    Load(path);
}

bool MaterialDataBase::ParseBaseText(const std::string& jsonText, BaseDef& out) {
    try { out = parseBase(json::parse(jsonText)); return true; }
    catch (const json::exception&) { return false; }
}

bool MaterialDataBase::ParseMaterialText(const std::string& jsonText, MaterialDef& out) {
    try { out = parseMaterial(json::parse(jsonText)); return true; }
    catch (const json::exception&) { return false; }
}