#pragma once
#include <vector>
#include <string>

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

    std::vector<int>         fieldNodeTypes;   // FieldNodeType‚ðint‚Å•Û‘¶
    std::vector<std::string> fieldNodeEnemyIds;
    std::vector<bool>        fieldNodeVisited;
};