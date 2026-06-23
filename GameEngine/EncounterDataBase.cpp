#include "EncounterDataBase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <random>
#include <windows.h>

using json = nlohmann::json;

std::vector<EncounterData> EncounterDataBase::m_data;

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