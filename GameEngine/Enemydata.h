#pragma once
#include "EnemyAction.h"
#include <vector>
#include <string>

struct EnemyData
{
    std::string id;          // Enemyの名前
    std::string textureName; // TextureManagerのキー
    int hp;
    float width;
    float height;
    bool isBoss;
    bool immovable;
    bool sequential = false;   // trueなら行動を定義順に1つずつ回す
    std::vector<std::pair<int, int>> gridShape;
    std::vector<EnemyAction> actions;
};