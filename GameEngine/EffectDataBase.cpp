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
        def.then = e.value("then", "");
        def.thenDelay = e.value("thenDelay", 0.0f);
        def.shake = e.value("shake", 0.0f);
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
            bd.texture = b.value("texture", "");
            def.bursts.push_back(bd);
        }
        if (e.contains("sheets"))
        {
            for (auto& s : e["sheets"])
            {
                SheetAnim sa;
                sa.texture = s.value("texture", "");
                sa.cols = s.value("cols", 1);
                sa.rows = s.value("rows", 1);
                sa.frames = s.value("frames", 0);
                sa.fps = s.value("fps", 20.0f);
                sa.scale = s.value("scale", 1.0f);
                sa.yOffset = s.value("yOffset", 0.0f);
                sa.loop = s.value("loop", false);
                sa.startFrame = s.value("startFrame", 0);
                sa.delay = s.value("delay", 0.0f);
                sa.autoFlipX = s.value("autoFlipX", false);
                sa.rotDeg = s.value("rotDeg", 0.0f);
                sa.travel = s.value("travel", 0.0f);
                if (s.contains("fromCamera"))
                {
                    auto f = s["fromCamera"];
                    sa.fromCamera = XMFLOAT3(f[0], f[1], f[2]);
                    sa.useCamera = true;
                }
                if (s.contains("offset")) { auto o = s["offset"]; sa.offset = XMFLOAT3(o[0], o[1], o[2]); }
                if (s.contains("vel")) { auto v = s["vel"];    sa.vel = XMFLOAT3(v[0], v[1], v[2]); }
                if (s.contains("color"))
                {
                    auto c = s["color"];
                    sa.color = XMFLOAT4(c[0], c[1], c[2], c[3]);
                }
                def.sheets.push_back(sa);
            }
        }
        s_effects[def.id] = def;
    }
}

const EffectDef* EffectDataBase::Get(const std::string& id)
{
    auto it = s_effects.find(id);
    return it != s_effects.end() ? &it->second : nullptr;
}