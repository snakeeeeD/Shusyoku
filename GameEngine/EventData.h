#pragma once
#include <string>
#include <vector>

struct EventOutcome { std::string type, param; int value = 0; };
struct EventChoice { std::string label, result; std::vector<EventOutcome> outcomes; };
struct EventDef { std::string id, title, desc; std::vector<EventChoice> choices; };