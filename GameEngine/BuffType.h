#pragma once

enum class BuffType
{
	// < --- バフ --- >
	
	// 攻撃系
	AttackUp,		// 攻撃力アップ

	// 防御系
	DefenseUp,		// 防御力アップ

	// 移動系
	MoveUp,			// 移動アップ

	// 回復系
	Regeneration,	// 毎ターン回復

	// < --- デバフ --- >

	// 攻撃系
	AttackDown,		// 攻撃系ダウン

	//　防御系
	DefenseDown,	// 防御力ダウン

	// 毒
	Poison,			// 毎ターンダメージ
};