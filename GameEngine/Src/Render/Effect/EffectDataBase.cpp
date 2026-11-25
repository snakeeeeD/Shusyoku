#include "EffectDataBase.h"
#include "EffectKinds.h"
#include "Se.h"
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::unordered_map<std::string, EffectDef> EffectDataBase::s_effects;
std::unordered_map<std::string, FxMaterial> EffectDataBase::s_materials;

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
        def.then = e.value("then", "");
        def.thenDelay = e.value("thenDelay", 0.0f);
        def.shake = e.value("shake", 0.0f);
        def.randomSide = e.value("randomSide", false);
        def.se = e.value("se", "");                     // sound effect file in Assets/Sound/se/
        def.seVol = e.value("seVol", 1.0f);
        if (!def.se.empty()) Audio::SetSeVolume(Se::Path(def.se), def.seVol);
        // 部品：今までの "bursts" "sheets" は、それぞれ "burst" "sheet" の種類の並び。新しい種類は "parts" に "type" を付けて書く
        auto addParts = [&](const char* key, const std::string& fixedType)
        {
            if (!e.contains(key)) return;
            for (auto& p : e[key])
            {
                std::string type = fixedType.empty() ? p.value("type", "") : fixedType;
                if (auto d = EffectKinds::Load(type, p)) def.parts.push_back(std::move(d));
            }
        };
        addParts("bursts", "burst");
        addParts("sheets", "sheet");
        addParts("parts", "");
        std::string id = def.id;
        s_effects[id] = std::move(def);
    }
    // エフェクト用シェーダーの材質（形・ノイズ・色の帯と、消え方などの数字）
    if (j.contains("materials"))
        for (auto& [id, m] : j["materials"].items())
        {
            FxMaterial fm;
            fm.shape = m.value("shape", ""); fm.noise = m.value("noise", ""); fm.ramp = m.value("ramp", "");
            fm.erodeStart = m.value("erodeStart", 0.5f); fm.erodeEnd = m.value("erodeEnd", 1.0f);
            fm.glow = m.value("glow", 1.0f); fm.noiseScale = m.value("noiseScale", 1.0f);
            if (m.contains("scroll")) fm.scroll = XMFLOAT2(m["scroll"][0], m["scroll"][1]);
            fm.additive = m.value("additive", false);
            fm.appear = m.value("appear", 0.0f);
            s_materials[id] = fm;
        }
}

const EffectDef* EffectDataBase::Get(const std::string& id)
{
    auto it = s_effects.find(id);
    return it != s_effects.end() ? &it->second : nullptr;
}

const FxMaterial* EffectDataBase::GetMaterial(const std::string& id)
{
    auto it = s_materials.find(id);
    return it != s_materials.end() ? &it->second : nullptr;
}
