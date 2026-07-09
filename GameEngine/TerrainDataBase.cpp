#include "TerrainDataBase.h"
#include "GameUtils.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::unordered_map<std::string, TerrainDef> TerrainDataBase::s_terrains;

void TerrainDataBase::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    json j;
    file >> j;

    for (auto& t : j["terrains"])
    {
        TerrainDef def;
        def.id = t["id"];
        def.name = ToWString(t["name"]);
        def.texture = t.value("texture", "");
        def.effect = t["effect"];
        def.value = t.value("value", 0);
        def.buffType = t.value("buffType", "");
        def.buffDuration = t.value("buffDuration", 0);
        def.persistent = t.value("persistent", false);
        def.duration = t.value("duration", 0);
        def.animFrames = t.value("animFrames", 0);
        def.animSpeed = t.value("animSpeed", 0.0f);
        def.aoe = t.value("aoe", false);

        auto c = t["color"];
        def.color = XMFLOAT4(c[0], c[1], c[2], c[3]);

        s_terrains[def.id] = def;
    }
}

const TerrainDef* TerrainDataBase::Get(const std::string& id)
{
    auto it = s_terrains.find(id);
    if (it != s_terrains.end())
        return &it->second;
    return nullptr;
}