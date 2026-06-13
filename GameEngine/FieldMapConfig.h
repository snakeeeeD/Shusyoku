#pragma once
#include "FieldNodeType.h"
#include <string>
#include <vector>

// 特定位置に配置するマスの設定
struct FixedNodeConfig
{
    int col;
    int row;
    FieldNodeType type;
    std::string enemyId;
};

// マスの種類ごとの上限設定
struct NodeTypeLimit
{
    FieldNodeType type;
    int maxCount;   // -1で無制限
    int weight;     // 出現重み（大きいほど出やすい）
};

struct FieldMapConfig
{
    std::vector<NodeTypeLimit> typeLimits;
    std::vector<FixedNodeConfig> fixedNodes; // 特定位置に固定配置するマス
};