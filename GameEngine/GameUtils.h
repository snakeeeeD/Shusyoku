#pragma once
#include "CardType.h"
#include "CardData.h"
#include "EnemyActionType.h"
#include "BuffType.h"
#include <string>
#include <windows.h>

// 文字列からRangeTypeに変換
inline RangeType StringToRangeType(const std::string& str)
{
    if (str == "Adjacent") return RangeType::Adjacent;
    if (str == "Cross")    return RangeType::Cross;
    if (str == "Area")     return RangeType::Area;
    if (str == "Diamond")  return RangeType::Diamond;
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

// 文字列からEnemyActionTypeに変換
inline EnemyActionType StringToActionType(const std::string& str)
{
    if (str == "Attack") return EnemyActionType::Attack;
    if (str == "Defend") return EnemyActionType::Defend;
    if (str == "Move")   return EnemyActionType::Move;
    if (str == "Buf")    return EnemyActionType::Buf;
    if (str == "Debuf")  return EnemyActionType::Debuf;
    return EnemyActionType::Attack;
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
    return CardEffectType::None;
}

inline BuffType StringToBuffType(const std::string& str)
{
    // バフ
    if (str == "AttackUp")    return BuffType::AttackUp;
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
    return BuffType::AttackUp;
}