#pragma once

#pragma once

enum class BuffType
{
    // < --- バフ --- >

    // 攻撃系
    AttackUp,       // 攻撃力アップ

    // 防御系
    DefenseUp,      // 防御力アップ
    Barricade,      // ブロック持ち越し
    Thorns,         // 隣接攻撃に反射ダメージ
    Momentum,       // 移動するたびにブロック+

    // 移動系
    MoveUp,         // 移動アップ
    Charge,         // 移動距離に応じて攻撃ダメージ+
    HitAndRun,      // 移動時に隣接敵にダメージ
    Reposition,     // 移動後、射程+1

    // < --- デバフ --- >

    // 攻撃系
    AttackDown,     // 攻撃系ダウン
    Weak,           // 与ダメージ割合減

    // 防御系
    DefenseDown,    // 防御力ダウン
    Frail,          // ブロック割合減
    Vulnerable,     // 受けるダメージ増加

    // 移動系
    Root,           // 移動不可
    Slow,           // 移動距離減少
    Burn,           // 移動するたびダメージ

    // 毒
    Poison,         // 毎ターンダメージ、スタック制
};