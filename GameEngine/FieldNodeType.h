#pragma once

enum class FieldNodeType
{
    Empty,    // 空（通過不可）
    Start,    // スタート
    Battle,   // バトル
    Rest,     // 休憩
    Shop,     // ショップ
    Elite,    // エリート
    Boss,     // ボス
    Event,    // イベント
    Treasure, // 財宝
};

inline const char* NodeIconName(FieldNodeType t)
{
    switch (t)
    {
    case FieldNodeType::Battle:   return "node_battle";
    case FieldNodeType::Rest:     return "node_rest";
    case FieldNodeType::Shop:     return "node_shop";
    case FieldNodeType::Elite:    return "node_elite";
    case FieldNodeType::Boss:     return "node_boss";
    case FieldNodeType::Event:    return "node_event";
    case FieldNodeType::Treasure: return "node_treasure";
    case FieldNodeType::Start:    return "node_start";
    default:                      return "";
    }
}

inline const wchar_t* NodeDisplayName(FieldNodeType t)
{
    switch (t)
    {
    case FieldNodeType::Battle:   return L"バトル";
    case FieldNodeType::Elite:    return L"エリート";
    case FieldNodeType::Boss:     return L"ボス";
    case FieldNodeType::Rest:     return L"休憩";
    case FieldNodeType::Shop:     return L"ショップ";
    case FieldNodeType::Event:    return L"イベント";
    case FieldNodeType::Treasure: return L"財宝";
    case FieldNodeType::Start:    return L"スタート";
    default:                      return L"";
    }
}
inline const wchar_t* NodeDesc(FieldNodeType t)
{
    switch (t)
    {
    case FieldNodeType::Battle:   return L"敵と戦闘。勝つとカード報酬。";
    case FieldNodeType::Elite:    return L"強敵。倒すとより多くの報酬。";
    case FieldNodeType::Boss:     return L"層の主。倒すと次の層へ進む。";
    case FieldNodeType::Rest:     return L"回復／カード強化／クラフトから1つ。";
    case FieldNodeType::Shop:     return L"ゴールドで購入・カード削除・素材売却。";
    case FieldNodeType::Event:    return L"ランダムな出来事が起こる。";
    case FieldNodeType::Treasure: return L"レリックや素材が手に入る。";
    case FieldNodeType::Start:    return L"スタート地点。";
    default:                      return L"";
    }
}