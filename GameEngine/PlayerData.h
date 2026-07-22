#pragma once
#include <vector>
#include <string>
#include <map>

struct PlayerData
{
    int hp;
    int maxHp;
    std::vector<std::string> deck;
    int currentNodeIndex;
    std::vector<bool> clearedNodes;
    int fieldPlayerCol;
    int fieldPlayerRow;
    int fieldSteps;

    int gold = 0;
    std::map<std::string, int> materials;   // 素材ID → 個数

    bool rewardRare = false;   // エリート撃破後のカード選択をレア寄りに（Save/Loadには入れない）

    std::vector<int>         fieldNodeTypes;   // FieldNodeTypeをintで保存
    std::vector<std::string> fieldNodeEnemyIds;
    std::vector<bool>        fieldNodeVisited;
};