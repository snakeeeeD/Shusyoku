#pragma once

#include "BuffType.h"
#include <string>

struct Buff
{
	BuffType	 type;			// バフの種類
	int			 value;			// 効果量
	int			 duration;		// 残りターン(-1で永続)
	std::wstring name;			// 表示名
	std::wstring description;	// 説明分
	bool		 isPermanent()const { return duration == -1; }
};