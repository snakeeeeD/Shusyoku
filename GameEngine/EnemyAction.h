#pragma once
#include "EnemyActionType.h"
#include "CardType.h" 
#include <string>
#include <vector>

struct EnemyAction
{
    EnemyActionType type;
    int value;    // ダメージ量など
    int range;    // 効果範囲
    RangeType       rangeType;
    int chance;   // 発生確率（0?100）
    std::wstring description; // 表示テキスト
    std::string buffType;
    int duration = 0;

    // 攻撃時デバフ付与用
    std::string onHitBuffType;
    int onHitValue = 0;
    int onHitDuration = 0;

    std::vector<EnemyAction> subActions;   // メイン行動に続けて実行する追加行動
};