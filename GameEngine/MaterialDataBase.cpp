#include "MaterialDataBase.h"
#include <fstream>
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
    return e;
}

void MaterialDataBase::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    json j; f >> j;

    for (auto& b : j["bases"]) {
        BaseDef d;
        d.id = b["id"]; d.name = b.value("name", "");
        d.desc = b.value("desc", "");
        d.type = b["type"]; d.rangeType = b["rangeType"];
        d.range = b.value("range", 0); d.cost = b.value("cost", 0);
        d.mainType = b.value("mainType", "None"); d.mainValue = b.value("mainValue", 0);
        s_bases[d.id] = d;
    }
    for (auto& m : j["materials"]) {
        MaterialDef d;
        d.id = m["id"]; d.name = m.value("name", "");
        d.desc = m.value("desc", ""); 
        d.cost = m.value("cost", 0);
        for (const char* k : { "Attack", "Skill", "Move", "Power", "all" })
            if (m.contains(k)) d.entries[k] = parseEntry(m[k]);
        s_materials[d.id] = d;
    }
}

const BaseDef* MaterialDataBase::GetBase(const std::string& id) {
    auto it = s_bases.find(id);
    return it != s_bases.end() ? &it->second : nullptr;
}
const MaterialDef* MaterialDataBase::GetMaterial(const std::string& id) {
    auto it = s_materials.find(id);
    return it != s_materials.end() ? &it->second : nullptr;
}