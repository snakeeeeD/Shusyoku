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

    // ---- 合成：素材の効果の付き方（CardDataBase.cpp の applyEntry と一致させる）----
    inline constexpr const char* CraftSlots[] = {
        "amplifyMain", "hits", "main", "sub", "onHit", "pierce", "dash", "range", "exhaust", "reckless", "none", "onArrival",
    };

    // ---- 合成：素材の効果を書く場所（土台の種類ごと。all はどの種類にも効く）----
    inline constexpr const char* CraftEntryKeys[] = { "Attack", "Skill", "Move", "Power", "all" };

    // ---- レリック：効果の種類（kind）----
    // コード側の RelicManager::SumValue("kind") / HasKind / OwnedByKind / PlayerDataManager::AddRelic と一致させる。
    // 新しい kind をコードに足したら、ここにも1行足す（エディタのプルダウンと参照切れチェッカーに出る）
    struct RelicKindInfo
    {
        const char* name;
        bool counting;      // カウント系（count 回でしきい値→effect を value）
        const char* hint;   // value の意味
    };

    inline constexpr RelicKindInfo RelicKinds[] = {
        // 戦闘開始時
        { "startBlock",    false, u8"戦闘開始時 ブロック+value" },
        { "startHeal",     false, u8"戦闘開始時 HP+value" },
        { "startBuffAtk",  false, u8"戦闘開始時 攻撃UP+value" },
        { "startDraw",     false, u8"戦闘開始時 追加でvalue枚引く" },
        { "startPoison",   false, u8"戦闘開始時 全ての敵に毒value" },
        // 毎ターン
        { "turnEnergy",    false, u8"毎ターン エナジー+value" },
        { "turnBuffAtk",   false, u8"毎ターン 攻撃UP+value" },
        { "turnCycle",     false, u8"毎ターン 追加でvalue枚引く" },
        { "blockRetain",   false, u8"ターン開始時 ブロックをvalue%残す" },
        // 戦闘中
        { "firstAttack",   false, u8"各戦闘の最初の攻撃 ダメージ+value" },
        { "multiHit",      false, u8"連撃攻撃のヒット数+value" },
        { "moveBlock",     false, u8"移動するたび ブロック+value" },
        { "moveRange",     false, u8"移動範囲+value" },
        { "poisonAdd",     false, u8"毒を与える量+value" },
        { "poisonSpread",  false, u8"毒の敵を倒すと周りの敵へ毒が移る（value不使用）" },
        { "knifeBonus",    false, u8"ナイフのダメージ+value" },
        { "trapDamage",    false, u8"罠のダメージ+value" },
        { "trapDebuff",    false, u8"罠のデバフ+value" },
        { "trapDraw",      false, u8"罠を置くとvalue枚引く" },
        // 撃破・勝利
        { "killGold",      false, u8"敵を倒すと ゴールド+value" },
        { "killHeal",      false, u8"敵を倒すと HP+value" },
        { "killStrength",  false, u8"敵を倒すと 攻撃UP+value" },
        { "winHeal",       false, u8"戦闘勝利時 HP+value" },
        { "eliteMaterial", false, u8"エリート撃破時 素材をvalue個" },
        // 戦闘外
        { "maxHp",         false, u8"取得時 最大HP+value" },
        { "steps",         false, u8"取得時 歩数+value" },
        { "restHeal",      false, u8"休憩の回復+value" },
        { "shopDiscount",  false, u8"ショップ価格 -value%" },
        { "rewardRare",    false, u8"カード報酬にレアが出やすい（value不使用）" },
        { "craftCostDown", false, u8"合成カードのコスト-value" },
        { "modSlots",      false, u8"合成の素材枠+value" },
        // カウント系（count 回使うたびに effect を value）
        { "attack",        true,  u8"攻撃カードをcount回使うたび" },
        { "skill",         true,  u8"スキルカードをcount回使うたび" },
        { "move",          true,  u8"移動カードをcount回使うたび" },
        { "card",          true,  u8"カードをcount回使うたび" },
    };

    // レリックのレアリティ（RelicManager::RandomDrop / ShopPool / RandomUnowned と一致させる）
    inline constexpr const char* RelicRarities[] = { "common", "uncommon", "rare", "shop", "boss", "event", "starter" };

    // カウント系レリックの発動効果（BattleScene::OnCardPlayed と一致させる）
    inline constexpr const char* RelicEffects[] = { "block", "energy" };

    inline const RelicKindInfo* FindRelicKind(const std::string& name)
    {
        for (const auto& k : RelicKinds) if (name == k.name) return &k;
        return nullptr;
    }

    inline std::vector<std::string> RelicKindNames()
    {
        std::vector<std::string> v;
        for (const auto& k : RelicKinds) v.push_back(k.name);
        return v;
    }

    // ---- イベント：結果の種類（SceneManager::ApplyOutcomes / EventPickerType と一致させる）----
    inline constexpr const char* EventOutcomeTypes[] = {
        "hp", "maxHp", "gold", "relic", "material", "randomMaterial", "addCard", "pickCard",
        "removeCard", "upgradeCard", "transformCard", "transformRelic", "battle", "shop", "treasure",
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
