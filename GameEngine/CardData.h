#pragma once
#include <string>
#include <vector>

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
    CreateCard,  // カード生成
    Knockback,   // ノックバック
    Pull,        // 引き寄せ
    Search,      // 山札検索
    Salvage,     // 捨て札回収
    PlaceTrap,   // 罠セット
    PlaceTrapArea,//罠を複数セット
    Detonate,    // 罠を即起爆
    DetonateAt,  // 指定した罠を即起爆
    DetonateChain,//連鎖
    UpgradeHand, // 手札を強化（この戦闘中のみ）
    Discard,     // 手札からランダムに捨てる

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
    std::string    cardId = "";       // CreateCard用
    std::string trapType = "";  // "Explosion", "Root", "Poison"
    bool           hasEffect = false;
};

struct CardData
{
    std::string  id;                                // カードのID
    std::wstring name;                              // カードの名前
    CardType     type;                              // カードの種類
    int          cost;                              // カードのコスト
    int          range;                             // 射程距離
    RangeType    rangeType;                         // 効果範囲の種類
    std::wstring description;                       // カードの説明文
    std::string  vfx;                               // 使用時に再生するエフェクト名（effects.json）
    CardRarity rarity = CardRarity::Common;         // レアリティ
    std::vector<std::string> tags;                  // カードの軸用のタグ
    bool generated = false;                         // 生成専用（ナイフ等）＝報酬/店/変化に出さない

    CardEffectData   mainEffect;   // メイン効果（ダメージ量やブロック量など）
    CardEffectData   onHitEffect;  // ヒット時効果（攻撃カードのみ）
    CardEffectData onHitEffect2;   // 2つ目のonHit（複数デバフ攻撃用）
    CardEffectData   subEffect;    // サブ効果（ドロー/エネルギー/回復/自己バフ）
    CardEffectData allEnemyEffect; // 使用時に全敵へ付与（バフ/デバフ共用）

    bool starter = false;   // 初期デッキ用（移動以外は報酬/店/変化に出さない）

    bool exhaust = false;   // 廃棄カードフラグ
    bool pierce = false;    // 貫通フラグ
    bool dash = false;      // moveカード以外に移動効果があるかフラグ
    int selfDamage = 0;     // 自傷ダメージ
    int hits = 1;         // 攻撃回数（マルチヒット）
};