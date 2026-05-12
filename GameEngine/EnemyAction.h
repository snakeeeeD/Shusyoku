#pragma once
#include "EnemyActionType.h"
#include <string>

struct EnemyAction
{
    EnemyActionType type;
    int value;    // ダメージ量など
    int range;    // 効果範囲
    int chance;   // 発生確率（0?100）
    std::wstring description; // 表示テキスト
};