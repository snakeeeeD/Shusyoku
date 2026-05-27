#pragma once
#include "CardType.h"
#include "EnemyActionType.h"
#include <string>
#include <windows.h>

// •¶Žš—ñ‚©‚çRangeType‚É•ÏŠ·
inline RangeType StringToRangeType(const std::string& str)
{
    if (str == "Adjacent") return RangeType::Adjacent;
    if (str == "Cross")    return RangeType::Cross;
    if (str == "Area")     return RangeType::Area;
    if (str == "Diamond")  return RangeType::Diamond;
    return RangeType::None;
}

// •¶Žš—ñ‚©‚çCardType‚É•ÏŠ·
inline CardType StringToCardType(const std::string& str)
{
    if (str == "Attack") return CardType::Attack;
    if (str == "Skill")  return CardType::Skill;
    if (str == "Move")   return CardType::Move;
    if (str == "Power")  return CardType::Power;
    return CardType::Attack;
}

// •¶Žš—ñ‚©‚çEnemyActionType‚É•ÏŠ·
inline EnemyActionType StringToActionType(const std::string& str)
{
    if (str == "Attack") return EnemyActionType::Attack;
    if (str == "Defend") return EnemyActionType::Defend;
    if (str == "Move")   return EnemyActionType::Move;
    if (str == "Buf")    return EnemyActionType::Buf;
    if (str == "Debuf")  return EnemyActionType::Debuf;
    return EnemyActionType::Attack;
}

// UTF-8•¶Žš—ñ‚©‚çwstring‚É•ÏŠ·
inline std::wstring ToWString(const std::string& str)
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}