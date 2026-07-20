#include "EffectDataBase.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::unordered_map<std::string, EffectDef> EffectDataBase::s_effects;

void EffectDataBase::Load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) return;

    json j;
    file >> j;

    for (auto& e : j["effects"])
    {
        EffectDef def;
        def.id = e["id"];
        for (auto& b : e["bursts"])
        {
            BurstDef bd;
            bd.count = b.value("count", 10);
            bd.speed = b.value("speed", 1.5f);
            bd.life = b.value("life", 0.5f);
            bd.scale = b.value("scale", 0.1f);
            bd.gravity = b.value("gravity", 4.0f);
            bd.drag = b.value("drag", 0.02f);
            auto cs = b["colorStart"];
            auto ce = b["colorEnd"];
            bd.colorStart = XMFLOAT4(cs[0], cs[1], cs[2], cs[3]);
            bd.colorEnd = XMFLOAT4(ce[0], ce[1], ce[2], ce[3]);
            def.bursts.push_back(bd);
        }
        s_effects[def.id] = def;
    }
}

const EffectDef* EffectDataBase::Get(const std::string& id)
{
    auto it = s_effects.find(id);
    return it != s_effects.end() ? &it->second : nullptr;
}