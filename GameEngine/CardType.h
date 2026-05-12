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
	None,       // 範囲なし
	Adjacent,   // 隣接1マス
	Cross,      // 十字
	Area,       // 範囲（半径N）
};

