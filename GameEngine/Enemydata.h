#pragma once
#include "EnemyAction.h"
#include <vector>
#include <string>

struct EnemyData
{
    std::string id;          // Enemy‚Ì–¼‘O
    std::string textureName; // TextureManager‚ÌƒL[
    int hp;
    int attack;
    float width;
    float height;
    bool isBoss;
    std::vector<std::pair<int, int>> gridShape; // gridSize
    std::vector<EnemyAction> actions;
};