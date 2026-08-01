#pragma once
#include <string>
#include <vector>

struct EventOutcome 
{
	std::string type, param; 
	int value = 0; 
};

struct EventChoice
{
	std::string label, result;
	std::vector<EventOutcome> outcomes; 
};

struct EventDef 
{ 
	int layer = 0;   // 0=‘S‘w‚Åo‚é, 1/2/3=‚»‚Ì‘wŒÀ’è
	std::string id, title, desc; 
	std::vector<EventChoice> choices; 
};