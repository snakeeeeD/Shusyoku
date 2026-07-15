#pragma once

enum class CardType
{
	Attack,		// アタック
	Skill,		// スキル
	Move,		// 移動
	Power,		// パワー
};

enum class RangeType
{
    None,
    Adjacent,   // 上下左右1マス
    Cross,      // 十字
    Area,       // 正方形
    Diamond,    // ひし形
    Diagonal,   // 斜め4方向
    DiagonalCross, // 十字＋斜め（全8方向）
    Line,       // 直線（貫通）
    Cone,       // 扇形（プレイヤー方向）
};

