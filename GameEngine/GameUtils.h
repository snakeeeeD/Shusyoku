#pragma once
#include "CardType.h"
#include "CardData.h"
#include "EnemyAction.h"
#include "BuffType.h"
#include "GridMap.h"
#include <string>
#include <windows.h>

// 文字列からRangeTypeに変換
inline RangeType StringToRangeType(const std::string& str)
{
    if (str == "Adjacent") return RangeType::Adjacent;
    if (str == "Cross")    return RangeType::Cross;
    if (str == "Area")     return RangeType::Area;
    if (str == "Diamond")  return RangeType::Diamond;
    if (str == "Diagonal")  return RangeType::Diagonal;
    if (str == "DiagonalCross")  return RangeType::DiagonalCross;
    if (str == "Line")  return RangeType::Line;
    if (str == "Cone") return RangeType::Cone;
    return RangeType::None;
}

// 文字列からCardTypeに変換
inline CardType StringToCardType(const std::string& str)
{
    if (str == "Attack") return CardType::Attack;
    if (str == "Skill")  return CardType::Skill;
    if (str == "Move")   return CardType::Move;
    if (str == "Power")  return CardType::Power;
    return CardType::Attack;
}

// UTF-8文字列からwstringに変換
inline std::wstring ToWString(const std::string& str)
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

inline CardEffectType StringToCardEffectType(const std::string& str)
{
    if (str == "Damage")      return CardEffectType::Damage;
    if (str == "Block")       return CardEffectType::Block;
    if (str == "Draw")        return CardEffectType::Draw;
    if (str == "AddEnergy")   return CardEffectType::AddEnergy;
    if (str == "ApplyBuff")   return CardEffectType::ApplyBuff;
    if (str == "ApplyDebuff") return CardEffectType::ApplyDebuff;
    if (str == "Heal")        return CardEffectType::Heal;
    if (str == "CreateCard")  return CardEffectType::CreateCard;
    if (str == "Knockback")   return CardEffectType::Knockback;
    if (str == "Pull")        return CardEffectType::Pull;
    if (str == "Search")      return CardEffectType::Search;
    if (str == "Salvage")     return CardEffectType::Salvage;
    if (str == "PlaceTrap")   return CardEffectType::PlaceTrap;
    if (str == "PlaceTrapArea")  return CardEffectType::PlaceTrapArea;
    if (str == "Detonate")    return CardEffectType::Detonate;
    if (str == "RecallTraps") return CardEffectType::RecallTraps;
    if (str == "DetonateAt")  return CardEffectType::DetonateAt;
    if (str == "DetonateChain")  return CardEffectType::DetonateChain;
    if (str == "PlaceDecoy")  return CardEffectType::PlaceDecoy;
    if (str == "UpgradeHand") return CardEffectType::UpgradeHand;
    if (str == "Discard")     return CardEffectType::Discard;
    if (str == "Catalyst") return CardEffectType::Catalyst;
    if (str == "DrainPoison") return CardEffectType::DrainPoison;
    if (str == "PoisonBurst") return CardEffectType::PoisonBurst;
    if (str == "BlockDamage") return CardEffectType::BlockDamage;
    return CardEffectType::None;
}

inline BuffType StringToBuffType(const std::string& str)
{
    // バフ
    if (str == "AttackUp")    return BuffType::AttackUp;
    if (str == "RangeUp") return BuffType::RangeUp;
    if (str == "DefenseUp")   return BuffType::DefenseUp;
    if (str == "Barricade")   return BuffType::Barricade;
    if (str == "Thorns")      return BuffType::Thorns;
    if (str == "Momentum")    return BuffType::Momentum;
    if (str == "MoveUp")      return BuffType::MoveUp;
    if (str == "Charge")      return BuffType::Charge;
    if (str == "HitAndRun")   return BuffType::HitAndRun;
    if (str == "Reposition")  return BuffType::Reposition;
    // デバフ
    if (str == "AttackDown")  return BuffType::AttackDown;
    if (str == "Weak")        return BuffType::Weak;
    if (str == "DefenseDown") return BuffType::DefenseDown;
    if (str == "Frail")       return BuffType::Frail;
    if (str == "Vulnerable")  return BuffType::Vulnerable;
    if (str == "Root")        return BuffType::Root;
    if (str == "Slow")        return BuffType::Slow;
    if (str == "Burn")        return BuffType::Burn;
    if (str == "Poison")      return BuffType::Poison;

    if (str == "AttackUpTurn") return BuffType::AttackUpTurn;
    if (str == "AttackGrowth") return BuffType::AttackGrowth;
    if (str == "Frenzy") return BuffType::Frenzy;
    if (str == "NoxiousFumes") return BuffType::NoxiousFumes;
    if (str == "ToxicRhythm") return BuffType::ToxicRhythm;

    if (str == "MoveLock") return BuffType::MoveLock;
    if (str == "Riposte") return BuffType::Riposte;

    if (str == "KnifePower") return BuffType::KnifePower;
    if (str == "KnifeThrow") return BuffType::KnifeThrow;
    if (str == "CardBlock") return BuffType::CardBlock;
    if (str == "KnifeGen") return BuffType::KnifeGen;

    if (str == "LastStand") return BuffType::LastStand;
    if (str == "DeepStand") return BuffType::DeepStand;
    if (str == "EmberTrail")  return BuffType::EmberTrail;

    return BuffType::AttackUp;
}

inline ApproachType StringToApproach(const std::string& s)
{
    if (s == "Toward") return ApproachType::Toward;
    if (s == "Dash")   return ApproachType::Dash;
    return ApproachType::None;
}

inline EffectKind StringToEffectKind(const std::string& s)
{
    if (s == "Block")      return EffectKind::Block;
    if (s == "Buff")       return EffectKind::Buff;
    if (s == "Debuff")     return EffectKind::Debuff;
    if (s == "MoveToward") return EffectKind::MoveToward;
    if (s == "MoveAway")   return EffectKind::MoveAway;
    if (s == "PullPlayer") return EffectKind::PullPlayer;
    if (s == "KnockbackPlayer") return EffectKind::KnockbackPlayer;
    if (s == "Summon")     return EffectKind::Summon;
    if (s == "Hazard") return EffectKind::Hazard;
    return EffectKind::Damage;
}

inline ApplyTo StringToApplyTo(const std::string& s)
{
    if (s == "Self")   return ApplyTo::Self;
    if (s == "Allies") return ApplyTo::Allies;
    return ApplyTo::Player;
}