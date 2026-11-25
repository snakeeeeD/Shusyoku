#pragma once
#include <string>
#include <vector>

struct EventOutcome 
{
	std::string type, param; 
	int value = 0; 
	int chance = 100;   // 発動確率%（省略時100=必ず）
};

struct EventChoice
{
	std::string label, result;
	std::vector<EventOutcome> outcomes; 
	std::string group;   // 枠の名前（EventDef::slots と合わせる。空＝枠に関係なく毎回出す）
};

struct EventDef 
{ 
	int layer = 0;
	int maxLayer = 0;   // layer～maxLayer の層で出る（省略時は layer と同じ＝その層だけ）
	bool random = true;
	std::string id, title, desc; std::vector<EventChoice> choices; 
	std::vector<std::string> slots;   // 枠（空でなければ、枠ごとに同じ group の選択肢から1つをランダムに出す）
};