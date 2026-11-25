#pragma once
#include "CardType.h"
#include <string>
#include <vector>

// 効果を当てるための移動（効果の"前"に起きる／予告の危険範囲に含む）
enum class ApproachType { None, Toward, Dash, Align };
// 範囲の中心をどこに置くか
enum class AnchorType { Self, Player, Fixed };

// 何が起きるか
enum class EffectKind {
    Damage, Block, Buff, Debuff, MoveToward,
    MoveAway, PullPlayer, KnockbackPlayer, Summon, Hazard, Heal, Coil, AddCard,
    Lunge, Retract   // 蛇：首を伸ばして噛みつく／伸ばした首を戻す
};

// 誰に効くか
enum class ApplyTo { Self, Player, Allies };
enum class WindupAt { Target, Mouth, Line, Self };   // 予備動作の演出を出す所：狙った所／口／列の端から端へ／敵自身

// 誰を狙うか
struct TargetSpec
{
    RangeType rangeType = RangeType::Adjacent;
    int  range = 1;
    int  minRange = 0;                        // ドーナツ（内側は当たらない）
    bool unavoidable = false;                 // 位置に関係なく必中
    ApproachType approach = ApproachType::None;
    int  moveRange = 1;                       // approach で動くマス数
    AnchorType anchor = AnchorType::Self;   // 範囲の中心（Self=敵中心/Player=プレイヤー/Fixed=座標）
    int  anchorCol = 0, anchorRow = 0;      // Fixed用のグリッド座標
};

struct Effect
{
    EffectKind  kind = EffectKind::Damage;
    int         value = 0;
    std::string buff;                         // Buff/Debuff で使う
    int         duration = 0;
    ApplyTo     applyTo = ApplyTo::Player;    // パース時に kind ごとの既定を入れる
    std::string summonId;   // Summonで召喚する敵ID（valueが体数）
    int hits = 1;   // 複数回攻撃（Damageのヒット数）
};

// いつ選ばれるか（行動の中身とは別レイヤー）
struct SelectRule
{
    std::string condition;                    // "" / near / far / hpBelow / turnAbove / turnMultiple / turnExact
    int conditionValue = 0;
    int chance = 100;
    int maxUses = 0;   // 1戦で使える回数（0=無制限）
};

struct EnemyAction
{
    std::wstring description;
    std::string  vfx;
    TargetSpec   target;                   // 主ターゲット
    std::vector<TargetSpec> extraTargets;  // 追加の範囲
    std::vector<Effect> effects;
    SelectRule   select;
    std::string  windupVfx;               // 着弾前に飛ばす演出(メテオ落下)
    float        windup = 0.0f;           // 着弾までの秒数（この間、行動は保留）
    std::string  areaVfx;                 // 攻撃範囲の各マスに出す演出（空なら area_burst）
    std::string  impactVfx;               // 着弾点に出す演出（当たり外れに関わらず）
    std::string  hitVfx;                  // 1発当たるたびにプレイヤーの所に出す演出（多段は1発ごと）。当たった時は vfx と範囲の演出を出さない
    std::string  selfVfx;                 // 行動した時に敵自身の位置で出す演出（バフ・構えなど。命中に関係なし）
    std::string  originVfx;               // 敵の口から攻撃の向きへ出す演出（ブレス・吐く攻撃。扇は扇の向き、ほかは狙ったマスの向き）
    std::string  aroundVfx;               // 周り全体の攻撃で、衝撃波の代わりに1つだけ出す演出（なぎ払いなど。範囲の大きさに合わせて拡大）
    std::string  allyVfx;                 // 行動した時、自分から味方の1体ずつへ飛ばす演出（楔を強化する。PlayFrom）
    WindupAt     windupAt = WindupAt::Target;   // "windupAt": "mouth" / "line" / "self"（書かなければ狙った所）
    float        waveTime = 0.0f;         // aroundVfx の波が範囲の外の縁まで届く秒数。0より大きいと、ダメージは波がプレイヤーに届いた時に入る

    // この効果を持っているか（引き寄せの行動か、などを調べる）
    bool HasEffect(EffectKind k) const
    {
        for (auto& e : effects) if (e.kind == k) return true;
        return false;
    }
};