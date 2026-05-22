#pragma once
#include "FieldNodeType.h"
#include <vector>

struct FieldNode
{
    FieldNodeType type;
    int col;
    int row;
    bool cleared;   // クリア済みか
    std::vector<int> nextNodeIndices; // 次のノードのインデックス
};