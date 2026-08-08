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
};

struct EventDef 
{ 
	int layer = 0;
	bool random = true;
	std::string id, title, desc; std::vector<EventChoice> choices; 
};