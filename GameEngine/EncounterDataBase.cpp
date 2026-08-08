#include "EncounterDataBase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <windows.h>

using json = nlohmann::json;

std::vector<EncounterData> EncounterDataBase::m_data;

static EscalationKind ParseEscKind(const std::string& s)
{
    if (s == "atkUp")     return EscalationKind::AtkUp;
    if (s == "addAction") return EscalationKind::AddAction;
    if (s == "addEnemy")  return EscalationKind::AddEnemy;
    return EscalationKind::HpUp;
}

static EncCategory ParseCategory(const std::string& s)
{
    if (s == "elite") return EncCategory::Elite;
    if (s == "boss")  return EncCategory::Boss;
    if (s == "event") return EncCategory::Event;
    return EncCategory::Normal;
}

void EncounterDataBase::Init()
{
    std::ifstream file("Assets/Data/encounters.json");
    if (!file.is_open()) return;

    json j = json::parse(file);

    for (auto& e : j["encounters"])
    {
        EncounterData data;
        for (auto& enemy : e["enemies"])
        {
            EncounterEnemy ee;
            ee.id = enemy["id"];
            ee.col = enemy["col"];
            ee.row = enemy["row"];
            data.enemies.push_back(ee);
        }
        data.layer = e.value("layer", 1);
        data.tier = e.value("tier", 1);
        data.id = e.value("id", "");
        data.category = ParseCategory(e.value("category", std::string("normal")));
        data.weight = e["weight"];
        if (e.contains("escalation"))
            for (auto& t : e["escalation"])
            {
                EscalationTier tier;
                tier.kind = ParseEscKind(t.value("kind", std::string("hpUp")));
                tier.value = t.value("value", 0);
                tier.id = t.value("id", std::string(""));
                tier.col = t.value("col", 0);
                tier.row = t.value("row", 0);
                data.escalation.push_back(tier);
            }
        m_data.push_back(data);
    }
}

const EncounterData* EncounterDataBase::GetByIndex(int index)
{
    if (index < 0 || index >= (int)m_data.size()) return nullptr;
    return &m_data[index];
}

int EncounterDataBase::GetCount()
{
    return (int)m_data.size();
}

void EncounterDataBase::Reload()
{
    m_data.clear();
    Init();
}

const EncounterData* EncounterDataBase::GetEncounter(int layer, EncCategory cat, int tier, int seed)
{
    std::vector<EncounterData*> cand;
    for (int tt = tier; tt >= 1 && cand.empty(); tt--)      // óvãÅtierÅ®ñ≥ÇØÇÍÇŒâ∫à Ç÷ÅBå©Ç¬Ç©Ç¡ÇΩç≈çÇtierÇæÇØçÃóp
        for (auto& enc : m_data)
            if (enc.layer == layer && enc.category == cat && enc.tier == tt) cand.push_back(&enc);
    if (cand.empty())   // ÇªÇÃëwÇ…ñ≥ÇØÇÍÇŒëw1Ç÷
        for (auto& enc : m_data)
            if (enc.layer == 1 && enc.category == cat) cand.push_back(&enc);
    if (cand.empty()) return nullptr;

    unsigned int h = (unsigned int)seed;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
    int total = 0; for (auto* e : cand) total += (e->weight > 0 ? e->weight : 1);
    int roll = (int)(h % (unsigned)total), acc = 0;
    for (auto* e : cand) { acc += (e->weight > 0 ? e->weight : 1); if (roll < acc) return e; }
    return cand.back();
}

const EncounterData* EncounterDataBase::GetById(const std::string& id)
{
    if (id.empty()) return nullptr;
    for (auto& enc : m_data)
        if (enc.id == id) return &enc;
    return nullptr;
}

const std::vector<EscalationTier>& EncounterDataBase::DefaultEscalation()
{
    static const std::vector<EscalationTier> def = {
        { EscalationKind::HpUp,      30, "", 0, 0 },   // 1ï‡ÅFHP+30%
        { EscalationKind::AtkUp,     25, "", 0, 0 },   // 2ï‡ÅFçUåÇ+25%
        { EscalationKind::AddAction,  0, "", 0, 0 },   // 3ï‡ÅFçsìÆ+1
        { EscalationKind::AddEnemy,   0, "reaper", 4, 6 }, // 4ï‡ÅFìGí«â¡
    };
    return def;
}