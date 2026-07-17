#pragma once
#include "EnemyAction.h"
#include <vector>
#include <string>

struct EnemyData
{
    std::string id;          // Enemy‚Ì–¼‘O
    std::string textureName; // TextureManager‚ÌƒL[
    int hp;
    float width;
    float height;
    bool isBoss;
    bool immovable;
    std::vector<std::pair<int, int>> gridShape; // gridSize
    std::vector<EnemyAction> actions;
};