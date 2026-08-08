#pragma once
#include "EventData.h"
#include <unordered_map>
#include <unordered_set>  
#include <string>
#include <vector> 

class EventDataBase {
public:
    static void Load(const std::string& path);
    static const EventDef* Get(const std::string& id);
    static std::string RandomId(int layer = 0);
    static std::vector<std::string> AllIds();
private:
    static std::unordered_map<std::string, EventDef> s_events;
    static std::unordered_set<std::string> s_usedIds;
};