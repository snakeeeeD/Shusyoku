#pragma once
#include "CardType.h"
#include "CardData.h"
#include "BuffType.h"
#include "EnemyAction.h"
#include <cstddef>
#include <string>
#include <vector>

// ======================================================================
// enum ⇔ JSON の文字列 の対応表（ここ1か所にまとめる）
//
// - エディタのプルダウンは、この表から選択肢を作る（打ち間違いが起きない）
// - JSON に表に無い文字列があれば、エディタの検証で見つけられる
// - ゲーム側の StringToXxx（GameUtils.h など）も、この表を引く形に置き換えられる
//   → 新しい enum 値を足す時は「この表に1行」だけで済むようになる
// ======================================================================

template <class E>
struct EnumName
{
    E value;
    const char* name;
};

namespace EnumNames
{
    // ---- 攻撃範囲の形 ----
    inline constexpr EnumName<RangeType> RangeTypes[] = {
        { RangeType::None,          "None" },
        { RangeType::Adjacent,      "Adjacent" },
        { RangeType::Cross,         "Cross" },
        { RangeType::Area,          "Area" },
        { RangeType::Diamond,       "Diamond" },
        { RangeType::Diagonal,      "Diagonal" },
        { RangeType::DiagonalCross, "DiagonalCross" },
        { RangeType::Line,          "Line" },
        { RangeType::Cone,          "Cone" },
        { RangeType::Square,        "Square" },
        { RangeType::Grid,          "Grid" },
        { RangeType::Row,           "Row" },
        { RangeType::Column,        "Column" },
    };

    // ---- 敵：攻撃のための移動 ----
    inline constexpr EnumName<ApproachType> Approaches[] = {
        { ApproachType::None,   "None" },
        { ApproachType::Toward, "Toward" },
        { ApproachType::Dash,   "Dash" },
        { ApproachType::Align,  "Align" },
    };

    // ---- 敵：範囲の中心（JSON では小文字）----
    inline constexpr EnumName<AnchorType> Anchors[] = {
        { AnchorType::Self,   "self" },
        { AnchorType::Player, "player" },
        { AnchorType::Fixed,  "fixed" },
    };

    // ---- 敵：効果の種類 ----
    inline constexpr EnumName<EffectKind> EffectKinds[] = {
        { EffectKind::Damage,          "Damage" },
        { EffectKind::Block,           "Block" },
        { EffectKind::Buff,            "Buff" },
        { EffectKind::Debuff,          "Debuff" },
        { EffectKind::MoveToward,      "MoveToward" },
        { EffectKind::MoveAway,        "MoveAway" },
        { EffectKind::PullPlayer,      "PullPlayer" },
        { EffectKind::KnockbackPlayer, "KnockbackPlayer" },
        { EffectKind::Summon,          "Summon" },
        { EffectKind::Hazard,          "Hazard" },
        { EffectKind::Heal,            "Heal" },
        { EffectKind::Coil,            "Coil" },
        { EffectKind::AddCard,         "AddCard" },
    };

    // ---- 敵：効果の対象 ----
    inline constexpr EnumName<ApplyTo> ApplyTos[] = {
        { ApplyTo::Self,   "Self" },
        { ApplyTo::Player, "Player" },
        { ApplyTo::Allies, "Allies" },
    };

    // ---- バフ／デバフ（JSON から指定できるもの）----
    // Invincible / HeadWeak はコード側だけで付ける表示用なので載せていない
    inline constexpr EnumName<BuffType> BuffTypes[] = {
        { BuffType::AttackUp,     "AttackUp" },
        { BuffType::RangeUp,      "RangeUp" },
        { BuffType::DefenseUp,    "DefenseUp" },
        { BuffType::Barricade,    "Barricade" },
        { BuffType::Thorns,       "Thorns" },
        { BuffType::Momentum,     "Momentum" },
        { BuffType::MoveUp,       "MoveUp" },
        { BuffType::Charge,       "Charge" },
        { BuffType::HitAndRun,    "HitAndRun" },
        { BuffType::Reposition,   "Reposition" },
        { BuffType::AttackDown,   "AttackDown" },
        { BuffType::Weak,         "Weak" },
        { BuffType::DefenseDown,  "DefenseDown" },
        { BuffType::Frail,        "Frail" },
        { BuffType::Vulnerable,   "Vulnerable" },
        { BuffType::Root,         "Root" },
        { BuffType::Slow,         "Slow" },
        { BuffType::Burn,         "Burn" },
        { BuffType::Poison,       "Poison" },
        { BuffType::AttackUpTurn, "AttackUpTurn" },
        { BuffType::AttackGrowth, "AttackGrowth" },
        { BuffType::Frenzy,       "Frenzy" },
        { BuffType::NoxiousFumes, "NoxiousFumes" },
        { BuffType::ToxicRhythm,  "ToxicRhythm" },
        { BuffType::MoveLock,     "MoveLock" },
        { BuffType::Riposte,      "Riposte" },
        { BuffType::KnifePower,   "KnifePower" },
        { BuffType::KnifeThrow,   "KnifeThrow" },
        { BuffType::CardBlock,    "CardBlock" },
        { BuffType::KnifeGen,     "KnifeGen" },
        { BuffType::LastStand,    "LastStand" },
        { BuffType::DeepStand,    "DeepStand" },
        { BuffType::EmberTrail,   "EmberTrail" },
        { BuffType::DrawPerTurn,  "DrawPerTurn" },
    };

    // ---- カード：効果の種類 ----
    inline constexpr EnumName<CardEffectType> CardEffectTypes[] = {
        { CardEffectType::None,          "None" },
        { CardEffectType::Damage,        "Damage" },
        { CardEffectType::Block,         "Block" },
        { CardEffectType::Draw,          "Draw" },
        { CardEffectType::AddEnergy,     "AddEnergy" },
        { CardEffectType::ApplyBuff,     "ApplyBuff" },
        { CardEffectType::ApplyDebuff,   "ApplyDebuff" },
        { CardEffectType::Heal,          "Heal" },
        { CardEffectType::CreateCard,    "CreateCard" },
        { CardEffectType::Knockback,     "Knockback" },
        { CardEffectType::Pull,          "Pull" },
        { CardEffectType::Search,        "Search" },
        { CardEffectType::Salvage,       "Salvage" },
        { CardEffectType::PlaceTrap,     "PlaceTrap" },
        { CardEffectType::PlaceTrapArea, "PlaceTrapArea" },
        { CardEffectType::Detonate,      "Detonate" },
        { CardEffectType::RecallTraps,   "RecallTraps" },
        { CardEffectType::DetonateAt,    "DetonateAt" },
        { CardEffectType::DetonateChain, "DetonateChain" },
        { CardEffectType::PlaceDecoy,    "PlaceDecoy" },
        { CardEffectType::UpgradeHand,   "UpgradeHand" },
        { CardEffectType::Discard,       "Discard" },
        { CardEffectType::Catalyst,      "Catalyst" },
        { CardEffectType::DrainPoison,   "DrainPoison" },
        { CardEffectType::PoisonBurst,   "PoisonBurst" },
        { CardEffectType::BlockDamage,   "BlockDamage" },
    };

    // ---- カード：種類 ----
    inline constexpr EnumName<CardType> CardTypes[] = {
        { CardType::Attack, "Attack" },
        { CardType::Skill,  "Skill" },
        { CardType::Move,   "Move" },
        { CardType::Power,  "Power" },
        { CardType::Status, "Status" },
        { CardType::Curse,  "Curse" },
    };

    // ---- カード：レアリティ ----
    inline constexpr EnumName<CardRarity> CardRarities[] = {
        { CardRarity::Common,   "Common" },
        { CardRarity::Uncommon, "Uncommon" },
        { CardRarity::Rare,     "Rare" },
    };

    // ---- カード：effects.json 以外に CardExecutor が特別扱いする vfx 名 ----
    inline constexpr const char* CardSpecialVfx[] = { "slash_spin", "slash_fan", "meteor_rain" };

    // ---- カード：説明文で使える差し込み（CardVisual::GetEffectText と一致させる）----
    inline constexpr const char* CardPlaceholders[] = {
        "{value}", "{sub}", "{onhit}", "{onhitdur}", "{onhit2}", "{onhit2dur}", "{hits}", "{ondiscard}",
    };

    // ---- 敵：行動を選ぶ条件（enum ではなく文字列のまま使われている。Enemy::ConditionMet と一致させる）----
    inline constexpr const char* Conditions[] = {
        "near", "far", "hpBelow", "turnAbove", "turnMultiple", "turnExact",
        "afterDodge", "allyBelow", "coiled", "invuln", "awake",
    };

    // ---- 敵：AddCard でカードを入れる先（Effect.buff に入れる）----
    inline constexpr const char* AddCardDestinations[] = { "deck", "hand", "discard" };

    // ==================================================================
    // 表を引く関数
    // ==================================================================

    // 文字列 → enum。見つかれば true
    template <class E, std::size_t N>
    inline bool TryParse(const EnumName<E>(&table)[N], const std::string& s, E& out)
    {
        for (const auto& e : table)
            if (s == e.name) { out = e.value; return true; }
        return false;
    }

    // 文字列 → enum。見つからなければ fallback
    template <class E, std::size_t N>
    inline E Parse(const EnumName<E>(&table)[N], const std::string& s, E fallback)
    {
        E v;
        return TryParse(table, s, v) ? v : fallback;
    }

    // enum → 文字列。表に無ければ nullptr
    template <class E, std::size_t N>
    inline const char* NameOf(const EnumName<E>(&table)[N], E v)
    {
        for (const auto& e : table)
            if (e.value == v) return e.name;
        return nullptr;
    }

    // 表に載っている文字列か
    template <class E, std::size_t N>
    inline bool Contains(const EnumName<E>(&table)[N], const std::string& s)
    {
        E v;
        return TryParse(table, s, v);
    }

    template <std::size_t N>
    inline bool Contains(const char* const (&list)[N], const std::string& s)
    {
        for (const char* n : list)
            if (s == n) return true;
        return false;
    }

    // プルダウン用の文字列一覧
    template <class E, std::size_t N>
    inline std::vector<std::string> Names(const EnumName<E>(&table)[N])
    {
        std::vector<std::string> out;
        for (const auto& e : table) out.push_back(e.name);
        return out;
    }

    template <std::size_t N>
    inline std::vector<std::string> Names(const char* const (&list)[N])
    {
        return std::vector<std::string>(list, list + N);
    }
}
