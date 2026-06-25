#pragma once
#include <string>
#include "CardType.h"

// カード効果の種類
enum class CardEffectType
{
    None,
    Damage,      // ダメージ
    Block,       // ブロック
    Draw,        // ドロー
    AddEnergy,   // エネルギー追加
    ApplyBuff,   // バフ付与（プレイヤー）
    ApplyDebuff, // デバフ付与（敵）
    Heal,        // HP回復
};

// カードレアリティ
enum class CardRarity
{
    Common,
    Uncommon,
    Rare,
};

// カード効果
struct CardEffectData
{
    CardEffectType type = CardEffectType::None;
    int            value = 0;
    int            duration = 0;      // バフ・デバフ用
    std::string    buffType = "";     // "Poison", "AttackUp"など
    bool           hasEffect = false;
};

struct CardData
{
    std::string  id;
    std::wstring name;
    CardType     type;
    int          cost;
    int          range;
    RangeType    rangeType;
    std::wstring description;
    CardRarity rarity = CardRarity::Common;

    CardEffectData   mainEffect;   // メイン効果（ダメージ量やブロック量など）
    CardEffectData   onHitEffect;  // ヒット時効果（攻撃カードのみ）
};