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
};