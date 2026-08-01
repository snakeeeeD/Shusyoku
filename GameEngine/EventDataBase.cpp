#include "EventDataBase.h"
#include <fstream>
#include <cstdlib>
#include <vector>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

std::unordered_map<std::string, EventDef> EventDataBase::s_events;

void EventDataBase::Load(const std::string& path)
{
    std::ifstream f(path); if (!f.is_open()) return;
    json j; f >> j;
    for (auto& e : j["events"])
    {
        EventDef d;
        d.id = e["id"]; d.title = e.value("title", ""); d.desc = e.value("desc", "");
        d.layer = e.value("layer", 0);
        for (auto& c : e["choices"])
        {
            EventChoice ch;
            ch.label = c.value("label", ""); ch.result = c.value("result", "");
            if (c.contains("outcomes"))
                for (auto& o : c["outcomes"])
                {
                    EventOutcome oc;
                    oc.type = o.value("type", ""); oc.param = o.value("param", ""); oc.value = o.value("value", 0);
                    ch.outcomes.push_back(oc);
                }
            d.choices.push_back(ch);
        }
        s_events[d.id] = d;
    }
}

const EventDef* EventDataBase::Get(const std::string& id)
{
    auto it = s_events.find(id); return it != s_events.end() ? &it->second : nullptr;
}

std::string EventDataBase::RandomId(int layer)
{
    std::vector<std::string> ids;
    for (auto& kv : s_events)
        if (kv.second.layer == 0 || kv.second.layer == layer) ids.push_back(kv.first);
    if (ids.empty()) return "";
    return ids[rand() % ids.size()];
}