#pragma once
#include "CardType.h"
#include <string>
#include <vector>

// 効果を当てるための移動（効果の"前"に起きる／予告の危険範囲に含む）
enum class ApproachType { None, Toward, Dash };

// 何が起きるか
enum class EffectKind { Damage, Block, Buff, Debuff, 
                        MoveToward, MoveAway, PullPlayer, KnockbackPlayer };

// 誰に効くか
enum class ApplyTo { Self, Player, Allies };

// 誰を狙うか
struct TargetSpec
{
    RangeType rangeType = RangeType::Adjacent;
    int  range = 1;
    int  minRange = 0;                        // ドーナツ（内側は当たらない）
    bool unavoidable = false;                 // 位置に関係なく必中
    ApproachType approach = ApproachType::None;
    int  moveRange = 1;                       // approach で動くマス数
};

struct Effect
{
    EffectKind  kind = EffectKind::Damage;
    int         value = 0;
    std::string buff;                         // Buff/Debuff で使う
    int         duration = 0;
    ApplyTo     applyTo = ApplyTo::Player;    // パース時に kind ごとの既定を入れる
};

// いつ選ばれるか（行動の中身とは別レイヤー）
struct SelectRule
{
    std::string condition;                    // "" / near / far / hpBelow / turnAbove / turnMultiple / turnExact
    int conditionValue = 0;
    int chance = 100;
};

struct EnemyAction
{
    std::wstring description;
    std::string  vfx;
    TargetSpec   target;
    std::vector<Effect> effects;
    SelectRule   select;
};