#pragma once
#include <string>
#include "CardType.h"


struct CardData
{
	std::string id;
	std::wstring name;
	CardType	type;
	int			cost;			// エネルギーコスト
	int			value;			// ダメージや効果量
	int			range;			// 射程
	RangeType rangeType;
	std::string description;
};

