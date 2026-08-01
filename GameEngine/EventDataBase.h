#pragma once
#include "EventData.h"
#include <unordered_map>
#include <string>

class EventDataBase {
public:
    static void Load(const std::string& path);
    static const EventDef* Get(const std::string& id);
    static std::string RandomId(int layer = 0);
private:
    static std::unordered_map<std::string, EventDef> s_events;
};