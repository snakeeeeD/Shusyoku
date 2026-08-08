#include "EventDataBase.h"
#include <fstream>
#include <cstdlib>
#include <vector>
#include <algorithm> 
#include <nlohmann/json.hpp>
using json = nlohmann::json;

std::unordered_map<std::string, EventDef> EventDataBase::s_events;
std::unordered_set<std::string> EventDataBase::s_usedIds;

void EventDataBase::Load(const std::string& path)
{
    std::ifstream f(path); if (!f.is_open()) return;
    json j; f >> j;
    for (auto& e : j["events"])
    {
        EventDef d;
        d.id = e["id"]; d.title = e.value("title", ""); d.desc = e.value("desc", "");
        d.layer = e.value("layer", 0);
        d.random = e.value("random", true);
        for (auto& c : e["choices"])
        {
            EventChoice ch;
            ch.label = c.value("label", ""); ch.result = c.value("result", "");
            if (c.contains("outcomes"))
                for (auto& o : c["outcomes"])
                {
                    EventOutcome oc;
                    oc.type = o.value("type", "");
                    oc.param = o.value("param", ""); 
                    oc.value = o.value("value", 0);
                    oc.chance = o.value("chance", 100);
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
        if ((kv.second.layer == 0 || kv.second.layer == layer) && kv.second.random && !s_usedIds.count(kv.first))
            ids.push_back(kv.first);

    if (ids.empty())   // この層で出せるイベントが尽きた → 履歴リセット
    {
        s_usedIds.clear();
        for (auto& kv : s_events)
            if ((kv.second.layer == 0 || kv.second.layer == layer) && kv.second.random) ids.push_back(kv.first);
    }
    if (ids.empty()) return "";

    std::string pick = ids[rand() % ids.size()];
    s_usedIds.insert(pick);   // 出したら記録
    return pick;
}

std::vector<std::string> EventDataBase::AllIds()
{
    std::vector<std::string> ids;
    for (auto& kv : s_events) ids.push_back(kv.first);
    std::sort(ids.begin(), ids.end());   // 表示順を安定させる
    return ids;
}