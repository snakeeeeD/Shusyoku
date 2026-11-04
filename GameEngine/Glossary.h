#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <set>
#include <map>
#include <nlohmann/json.hpp>
#include "BuffInfo.h"
#include "EnumNames.h"
#include "TerrainDataBase.h"
#include "CardTooltip.h"
#include "CardDataBase.h"
#include "Faith.h"
#include "GameUtils.h"
#include "GlossaryLink.h"
#include "PlayerDataManager.h"
#include "FieldNodeType.h"
#include <algorithm>

// 用語集の1項目
struct GlossaryEntry
{
    std::string id;
    std::wstring category, name, desc;
    std::string icon;                     // TextureManager のキー（無ければ空）
    std::vector<std::string> related;     // 関連する用語の id
    std::string clip;                     // 説明の動画（Assets/Video/xxx.mpg。無ければ空）
    int unlockBossLayer = 0;              // この層のボスに着くまで隠す（ネタバレ防止。0なら常に見せる）
};

// 用語集：glossary.json の手書きの概念 ＋ 今ある説明（バフ・床・カードのキーワード）を自動で集める
class Glossary
{
public:
    static const std::vector<GlossaryEntry>& All() { Ensure(); return s_entries; }
    static const std::vector<std::wstring>& Categories() { Ensure(); return s_cats; }

    // 今の周回で、その層のボスに着いたか（ボスのマスにいる、または先の層にいる）
    static bool ReachedBoss(int layer)
    {
        const auto& pd = PlayerDataManager::GetData();
        if (pd.layer > layer) return true;
        if (pd.layer < layer) return false;
        const int FIELD_ROWS = 7;   // FieldScene のマップの行数と一致（マスの番号は col * 行数 + row）
        int i = pd.fieldPlayerCol * FIELD_ROWS + pd.fieldPlayerRow;
        return i >= 0 && i < (int)pd.fieldNodeTypes.size() && pd.fieldNodeTypes[i] == (int)FieldNodeType::Boss;
    }
    static bool Locked(const GlossaryEntry& e)
    {
        return e.unlockBossLayer > 0 && !ReachedBoss(e.unlockBossLayer);
    }

    static const GlossaryEntry* Get(const std::string& id)
    {
        Ensure();
        for (auto& e : s_entries) if (e.id == id) return &e;
        return nullptr;
    }
    static const GlossaryEntry* FindByName(const std::wstring& name)
    {
        Ensure();
        for (auto& e : s_entries) if (e.name == name) return &e;
        return nullptr;
    }

    // 説明文の中の用語を探す（長い名前を優先、同じ用語は最初の1回だけ。exclude は自分の名前）
    static std::vector<TermHit> FindTerms(const std::wstring& text, const std::wstring& exclude)
    {
        Ensure();
        std::vector<const GlossaryEntry*> order;
        for (auto& e : s_entries) if (!e.name.empty() && e.name != exclude && !Locked(e)) order.push_back(&e);
        std::stable_sort(order.begin(), order.end(),
            [](const GlossaryEntry* a, const GlossaryEntry* b) { return a->name.size() > b->name.size(); });
        std::vector<bool> used(text.size(), false);
        std::vector<TermHit> hits;
        for (auto* e : order)
            for (size_t p = text.find(e->name); p != std::wstring::npos; p = text.find(e->name, p + 1))
            {
                bool free = true;
                for (size_t k = p; k < p + e->name.size(); k++) if (used[k]) { free = false; break; }
                if (!free) continue;
                for (size_t k = p; k < p + e->name.size(); k++) used[k] = true;
                hits.push_back({ (unsigned)p, (unsigned)e->name.size(), e->id });
                break;                                       // 同じ用語は最初の1回だけ
            }
        std::sort(hits.begin(), hits.end(), [](const TermHit& a, const TermHit& b) { return a.start < b.start; });
        return hits;
    }

    // どこからでも「このページを開いて」と頼める（SceneManager が次のフレームで開く）
    static void RequestOpen(const std::string& id) { s_request = id; s_hasRequest = true; }
    static bool TakeRequest(std::string& id)
    {
        if (!s_hasRequest) return false;
        id = s_request; s_hasRequest = false; return true;
    }

private:
    static void Ensure() { if (!s_loaded) Load(); }

    static std::wstring Fill(std::wstring s)
    {
        auto rep = [&](const std::wstring& key, const std::wstring& val) {
            for (size_t p; (p = s.find(key)) != std::wstring::npos; ) s.replace(p, key.size(), val);
            };
        rep(L"{FAITH_MAX}", std::to_wstring(Faith::MAX));
        rep(L"{SANCT_ATK}", std::to_wstring(Faith::AttackBonus(Faith::MAX)));
        rep(L"{value}", L"N");
        rep(L"{pct}", L"N");
        return s;
    }

    static void Add(GlossaryEntry e, std::set<std::wstring>& names)
    {
        if (e.name.empty() || names.count(e.name)) return;   // 同じ名前は先に登録したほうを使う
        names.insert(e.name);
        e.desc = Fill(e.desc);
        s_entries.push_back(std::move(e));
    }

    static void Load()
    {
        s_loaded = true;
        GlossaryLink::find = &Glossary::FindTerms;
        GlossaryLink::open = &Glossary::RequestOpen;
        s_entries.clear(); s_cats.clear();
        std::set<std::wstring> names;
        std::map<std::string, std::string> clipMap;   // 自動で集めた用語にも動画を付ける（id → 動画）
        std::set<std::string> hiddenIds;              // 用語集に出さない項目（ゲームには残す、用語としては重複するものなど）

        // ① 手書きの概念
        std::ifstream f("Assets/Data/glossary.json");
        if (f)
        {
            nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
            if (!j.is_discarded())
            {
                for (auto& c : j.value("categories", nlohmann::json::array()))
                    s_cats.push_back(ToWString(c.get<std::string>()));
                nlohmann::json clips = j.value("clips", nlohmann::json::object());   // 一時オブジェクトの .items() を直接回すと先に消えて落ちるので、変数に受ける
                for (auto& [id, path] : clips.items())
                    clipMap[id] = path.get<std::string>();
                for (auto& h : j.value("hidden", nlohmann::json::array()))
                    hiddenIds.insert(h.get<std::string>());
                for (auto& e : j.value("entries", nlohmann::json::array()))
                {
                    GlossaryEntry g;
                    g.id = e.value("id", "");
                    g.category = ToWString(e.value("category", ""));
                    g.name = ToWString(e.value("name", ""));
                    g.desc = ToWString(e.value("desc", ""));
                    g.icon = e.value("icon", "");
                    g.unlockBossLayer = e.value("unlockBossLayer", 0);
                    g.clip = e.value("clip", "");
                    for (auto& r : e.value("related", nlohmann::json::array())) g.related.push_back(r.get<std::string>());
                    Add(g, names);
                }
            }
        }

        // ② バフ・デバフ（BuffInfo の説明をそのまま使う。内部用・未使用のものは除く）
        static const BuffType hidden[] = { BuffType::HeadWeak, BuffType::Invincible, BuffType::Molt,
            BuffType::HolyGround, BuffType::Sanctuary, BuffType::Reposition, BuffType::MoveUp,
            BuffType::AttackDown, BuffType::DefenseDown, BuffType::Frail, BuffType::EmberTrail };
        static const BuffType debuffs[] = { BuffType::Weak, BuffType::Vulnerable, BuffType::Root,
            BuffType::Slow, BuffType::Burn, BuffType::Poison, BuffType::Mire, BuffType::MoveLock };
        for (auto& b : EnumNames::BuffTypes)
        {
            bool skip = false;
            for (auto h : hidden) if (h == b.value) skip = true;
            if (skip) continue;
            bool debuff = false;
            for (auto d : debuffs) if (d == b.value) debuff = true;
            const BuffInfo& info = BuffInfo::Get(b.value);
            GlossaryEntry g;
            g.id = std::string("buff_") + b.name;
            g.category = debuff ? L"デバフ" : L"バフ";
            g.name = info.name;
            g.desc = info.description;
            g.icon = info.texture;
            Add(g, names);
        }

        // ③ 罠と床（terrains.json の一覧。説明はマスのホバーと同じ文）
        std::ifstream tf("Assets/Data/terrains.json");
        if (tf)
        {
            nlohmann::json j = nlohmann::json::parse(tf, nullptr, false);
            if (!j.is_discarded())
                for (auto& t : j.value("terrains", nlohmann::json::array()))
                {
                    const TerrainDef* def = TerrainDataBase::Get(t.value("id", ""));
                    if (!def) continue;
                    GlossaryEntry g;
                    g.id = "terrain_" + def->id;
                    g.category = L"罠と床";
                    g.name = def->name;
                    for (auto& l : CardTooltip::TrapLines(def, def->value)) g.desc += (g.desc.empty() ? L"" : L"\n") + l;
                    g.desc = (def->persistent ? L"床：" : L"罠：") + g.desc;
                    g.icon = def->texture;
                    g.related.push_back(def->persistent ? "floor" : "trap");
                    Add(g, names);
                }
        }

        // ④ カードのキーワード（カードの横に出る説明と同じ文）
        for (auto& [id, data] : CardDataBase::GetAll())
            for (auto& [n, d] : CardTooltip::Entries(&data))
            {
                GlossaryEntry g;
                g.id = "kw_" + std::to_string(s_entries.size());
                g.category = L"カード";
                g.name = n;
                g.desc = d;
                Add(g, names);
            }

        for (auto& e : s_entries)                     // 「clips」で指定した動画（罠の種類など自動の用語用）
            if (e.clip.empty() && clipMap.count(e.id)) e.clip = clipMap[e.id];
        s_entries.erase(std::remove_if(s_entries.begin(), s_entries.end(),   // 「hidden」の項目は最後に外す
            [&](const GlossaryEntry& e) { return hiddenIds.count(e.id) > 0; }), s_entries.end());

        if (s_cats.empty()) s_cats = { L"基本", L"信仰", L"罠と床", L"バフ", L"デバフ", L"カード", L"敵" };
    }

    static inline std::vector<GlossaryEntry> s_entries;
    static inline std::vector<std::wstring> s_cats;
    static inline bool s_loaded = false;
    static inline std::string s_request;
    static inline bool s_hasRequest = false;
};
