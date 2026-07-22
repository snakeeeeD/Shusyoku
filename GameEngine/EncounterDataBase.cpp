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
        data.rank = e["rank"];
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

const EncounterData* EncounterDataBase::GetRandom(int rank)
{
    std::vector<EncounterData*> candidates;
    int totalWeight = 0;

    for (auto& enc : m_data)
    {
        if (enc.rank == rank)
        {
            candidates.push_back(&enc);
            totalWeight += enc.weight;
        }
    }

    if (candidates.empty()) return nullptr;

    static std::mt19937 rng(std::random_device{}());
    int roll = std::uniform_int_distribution<>(0, totalWeight - 1)(rng);

    for (auto* enc : candidates)
    {
        roll -= enc->weight;
        if (roll < 0) return enc;
    }

    return candidates.back();
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

const EncounterData* EncounterDataBase::GetByRankSeed(int rank, int seed)
{
    std::vector<EncounterData*> candidates;
    for (auto& enc : m_data)
        if (enc.rank == rank) candidates.push_back(&enc);
    if (candidates.empty()) return nullptr;

    // ビット混合ハッシュ（線形の偏りを壊す）
    unsigned int h = (unsigned int)seed;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;

    return candidates[h % candidates.size()];
}

const std::vector<EscalationTier>& EncounterDataBase::DefaultEscalation()
{
    static const std::vector<EscalationTier> def = {
        { EscalationKind::HpUp,      30, "", 0, 0 },   // 1歩：HP+30%
        { EscalationKind::AtkUp,     25, "", 0, 0 },   // 2歩：攻撃+25%
        { EscalationKind::AddAction,  0, "", 0, 0 },   // 3歩：行動+1
        { EscalationKind::AddEnemy,   0, "reaper", 4, 6 }, // 4歩：敵追加
    };
    return def;
}