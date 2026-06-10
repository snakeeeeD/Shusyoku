#pragma once
#include "FieldNodeType.h"
#include <vector>
#include <string>

struct FieldNode
{
    FieldNodeType type;
    int col;
    int row;
    bool visited; // 一度でも訪れたか
    std::vector<int> nextNodeIndices;
    std::string enemyId; // バトルマス用
};