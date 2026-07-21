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
    std::map<std::string, int> materials;   // ‘fŞID ¨ ŒÂ”

    std::vector<int>         fieldNodeTypes;   // FieldNodeType‚ğint‚Å•Û‘¶
    std::vector<std::string> fieldNodeEnemyIds;
    std::vector<bool>        fieldNodeVisited;
};