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
    float drawZ = 0.4f;   // 描画のZオフセット（大型キャラの足元合わせ用）
    float mouthX = 0.3f, mouthY = 0.5f;   // 口の位置（絵の真ん中から顔の向きへ絵の幅の何割／足元から絵の高さの何割）。enemies.json の "mouth": [x, y]
    bool isBoss;
    bool immovable;
    bool sequential = false;   // trueなら行動を定義順に1つずつ回す
    std::vector<std::pair<int, int>> gridShape;
    std::vector<EnemyAction> actions;
    std::vector<DropDef> drops;
    bool snake = false;
    std::string moveStyle = "walk";   // 移動の動き方（walk / hop / float / glide。MoveFeel）
};