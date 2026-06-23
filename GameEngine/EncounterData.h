#pragma once
#include <vector>
#include <string>

struct EncounterEnemy
{
    std::string id;
    int col;
    int row;
};

struct EncounterData
{
    std::vector<EncounterEnemy> enemies;
    int rank;
    int weight;
};