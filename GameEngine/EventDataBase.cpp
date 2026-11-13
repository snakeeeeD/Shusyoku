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
        d.maxLayer = e.value("maxLayer", d.layer);
        d.random = e.value("random", true);
        d.slots = e.value("slots", std::vector<std::string>{});
        for (auto& c : e["choices"])
        {
            EventChoice ch;
            ch.label = c.value("label", ""); ch.result = c.value("result", "");
            ch.group = c.value("group", "");
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

void EventDataBase::Reload(const std::string& path)
{
    s_events.clear();
    Load(path);
}

const EventDef* EventDataBase::Get(const std::string& id)
{
    auto it = s_events.find(id); return it != s_events.end() ? &it->second : nullptr;
}

EventDef EventDataBase::Roll(const std::string& id)
{
    const EventDef* src = Get(id);
    if (!src) return {};
    EventDef d = *src;
    if (d.slots.empty()) return d;

    d.choices.clear();
    std::vector<int> used;
    for (const auto& slot : d.slots)   // 枠ごとに、その group から（まだ出していない物を）1つ
    {
        std::vector<int> cand;
        for (int i = 0; i < (int)src->choices.size(); i++)
            if (src->choices[i].group == slot && std::find(used.begin(), used.end(), i) == used.end())
                cand.push_back(i);
        if (cand.empty()) continue;
        int pick = cand[rand() % cand.size()];
        used.push_back(pick);
        d.choices.push_back(src->choices[pick]);
    }
    for (const auto& c : src->choices)   // group の無い選択肢は毎回出す（「何もしない」など）
        if (c.group.empty()) d.choices.push_back(c);
    return d;
}

static bool InLayer(const EventDef& d, int layer)   // 0＝全層、それ以外は layer～maxLayer
{
    return d.layer == 0 || (d.layer <= layer && layer <= d.maxLayer);
}

std::string EventDataBase::RandomId(int layer)
{
    std::vector<std::string> ids;
    for (auto& kv : s_events)
        if (InLayer(kv.second, layer) && kv.second.random && !s_usedIds.count(kv.first))
            ids.push_back(kv.first);

    if (ids.empty())   // この層で出せるイベントが尽きた → 履歴リセット
    {
        s_usedIds.clear();
        for (auto& kv : s_events)
            if (InLayer(kv.second, layer) && kv.second.random) ids.push_back(kv.first);
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