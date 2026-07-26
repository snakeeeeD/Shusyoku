#pragma once
#include "EnemyAction.h"
#include <vector>
#include <string>

struct DropDef {
    std::string id;
    int chance = 0;
    int min = 1, max = 1;
    bool rare = false;
};

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
    std::vector<DropDef> drops;
};