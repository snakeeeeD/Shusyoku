#include "RefCheck.h"

#ifdef _DEBUG

#include "EditorCommon.h"
#include "EnumNames.h"
#include "TextureManager.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace EditorCommon;

namespace
{
    using Severity = RefCheck::Severity;
    using Problem = RefCheck::Problem;

    const char* DATA_DIR = "Assets/Data/";

    // ==================================================================
    // 表１：id の定義（どのファイルのどの項目が、どの一覧の名前になるか）
    // 同じ一覧名を複数行に書くと、まとめて1つの一覧になる（例：素材は bases と materials）
    // ==================================================================
    struct DefRule
    {
        const char* set;    // 一覧の名前
        const char* file;
        const char* path;   // 項目までの道のり（下の「パスの書き方」）
    };

    const DefRule DEFS[] = {
        { "enemy",     "enemies.json",    "enemies[].id" },
        { "card",      "cards.json",      "cards[].id" },
        { "effect",    "effects.json",    "effects[].id" },
        { "encounter", "encounters.json", "encounters[].id" },
        { "event",     "events.json",     "events[].id" },
        { "relic",     "relics.json",     "relics[].id" },
        { "material",  "materials.json",  "bases[].id" },
        { "material",  "materials.json",  "materials[].id" },
        { "terrain",   "terrains.json",   "terrains[].id" },
    };

    // ==================================================================
    // 表２：参照（この項目は、この一覧の名前を指しているはず）
    //
    // パスの書き方：
    //   a.b      … オブジェクトの項目 a の中の b
    //   a[]      … 配列 a の要素すべて
    //   *        … オブジェクトのすべての項目（素材の Attack/Skill/Power… をまとめて見る時など）
    //   #keys    … オブジェクトの「キー名」そのもの（セーブデータの materials など）
    //   行き着いた値が文字列ならそれを、文字列の配列なら全部を調べる。項目が無い・空文字なら調べない。
    //
    // 一覧の書き方（set）：
    //   enemy / card / …       … 表１で作った一覧、または下の固定の一覧
    //   a|b                    … どちらかにあれば OK
    //   lit:x,y                … その場で書いた名前だけ OK
    //   texture                … 読み込み済みのテクスチャ名（TextureManager）
    //   cardRef                … カード id（強化版 "id+" と合成カード "CRAFT:..." も OK）
    //
    // when：同じオブジェクトの項目 whenKey が whenValue の時だけ調べる（例：kind が Summon の時の summonId）
    // ==================================================================
    struct RefRule
    {
        const char* file;
        const char* path;
        const char* set;
        const char* whenKey = nullptr;
        const char* whenValue = nullptr;
        Severity severity = Severity::Error;
    };

    const RefRule RULES[] = {
        // ---- エンカウント ----
        { "encounters.json", "encounters[].enemies[].id",    "enemy" },
        { "encounters.json", "encounters[].escalation[].id", "enemy" },

        // ---- 敵 ----
        { "enemies.json", "enemies[].texture", "texture", nullptr, nullptr, Severity::Warning },
        { "enemies.json", "enemies[].drops[].id", "material" },
        { "enemies.json", "enemies[].actions[].target.rangeType",        "rangeType" },
        { "enemies.json", "enemies[].actions[].target.approach",         "approach" },
        { "enemies.json", "enemies[].actions[].target.anchor",           "anchor" },
        { "enemies.json", "enemies[].actions[].extraTargets[].rangeType", "rangeType" },
        { "enemies.json", "enemies[].actions[].extraTargets[].approach",  "approach" },
        { "enemies.json", "enemies[].actions[].extraTargets[].anchor",    "anchor" },
        { "enemies.json", "enemies[].actions[].select.condition",        "condition" },
        { "enemies.json", "enemies[].actions[].vfx",       "effect" },
        { "enemies.json", "enemies[].actions[].selfVfx",   "effect" },
        { "enemies.json", "enemies[].actions[].windupVfx", "effect" },
        { "enemies.json", "enemies[].actions[].areaVfx",   "effect" },
        { "enemies.json", "enemies[].actions[].impactVfx", "effect" },
        { "enemies.json", "enemies[].actions[].effects[].kind",     "enemyEffectKind" },
        { "enemies.json", "enemies[].actions[].effects[].applyTo",  "applyTo" },
        { "enemies.json", "enemies[].actions[].effects[].buff",     "buff",          "kind", "Buff" },
        { "enemies.json", "enemies[].actions[].effects[].buff",     "buff",          "kind", "Debuff" },
        { "enemies.json", "enemies[].actions[].effects[].buff",     "addCardDest",   "kind", "AddCard" },
        { "enemies.json", "enemies[].actions[].effects[].summonId", "enemy",         "kind", "Summon" },
        { "enemies.json", "enemies[].actions[].effects[].summonId", "cardRef",       "kind", "AddCard" },

        // ---- カード ----
        { "cards.json", "cards[].type",              "cardType" },
        { "cards.json", "cards[].rarity",            "rarity" },
        { "cards.json", "cards[].rangeType",         "rangeType" },
        { "cards.json", "cards[].upgrade.rangeType", "rangeType" },
        { "cards.json", "cards[].vfx",               "effect|cardSpecialVfx" },
        { "cards.json", "cards[].*.type",            "cardEffect" },   // mainEffect / subEffect / onHitEffect …
        { "cards.json", "cards[].*.buffType",        "buff" },
        { "cards.json", "cards[].*.cardId",          "cardRef" },
        { "cards.json", "cards[].*.trapType",        "terrain" },

        // ---- イベント ----
        { "events.json", "events[].choices[].outcomes[].type",  "eventOutcome" },
        { "events.json", "events[].choices[].outcomes[].param", "relic",                      "type", "relic" },
        { "events.json", "events[].choices[].outcomes[].param", "material",                   "type", "material" },
        { "events.json", "events[].choices[].outcomes[].param", "cardRef",                    "type", "addCard" },
        { "events.json", "events[].choices[].outcomes[].param", "encounter|lit:event,normal", "type", "battle" },
        { "events.json", "events[].choices[].outcomes[].param", "lit:rare,upgraded",          "type", "pickCard" },

        // ---- 合成素材 ----
        { "materials.json", "bases[].type",          "cardType" },
        { "materials.json", "bases[].rangeType",     "rangeType" },
        { "materials.json", "bases[].mainType",      "cardEffect" },
        { "materials.json", "bases[].vfx",           "effect|cardSpecialVfx" },
        { "materials.json", "materials[].*.type",     "cardEffect" },   // Attack / Skill / Power / all …
        { "materials.json", "materials[].*.buffType", "buff" },
        { "materials.json", "materials[].*.vfx",      "effect|cardSpecialVfx" },
        { "materials.json", "materials[].*.slot",     "craftSlot" },
        { "materials.json", "materials[].*.trapType", "terrain" },

        // ---- レリック ----
        { "relics.json", "relics[].kind",   "relicKind" },
        { "relics.json", "relics[].rarity", "relicRarity" },
        { "relics.json", "relics[].effect", "relicEffect" },

        // ---- 地形・罠 ----
        { "terrains.json", "terrains[].texture",  "texture", nullptr, nullptr, Severity::Warning },
        { "terrains.json", "terrains[].buffType", "buff" },
        { "terrains.json", "terrains[].vfx",      "effect" },

        // ---- エフェクト ----
        { "effects.json", "effects[].bursts[].texture", "texture", nullptr, nullptr, Severity::Warning },
        { "effects.json", "effects[].sheets[].texture", "texture", nullptr, nullptr, Severity::Warning },
        { "effects.json", "effects[].then",             "effect" },

        // ---- セーブデータ（遊んでいる途中の状態。消したカードなどが残っていると警告）----
        { "playerdata.json", "deck[]",          "cardRef",  nullptr, nullptr, Severity::Warning },
        { "playerdata.json", "relics[]",        "relic",    nullptr, nullptr, Severity::Warning },
        { "playerdata.json", "materials.#keys", "material", nullptr, nullptr, Severity::Warning },
    };

    // ==================================================================
    // 固定の一覧（enum の対応表などから作る）
    // ==================================================================
    template <class T>
    void AddNames(std::set<std::string>& out, const T& list)
    {
        for (auto& n : EnumNames::Names(list)) out.insert(n);
    }

    std::map<std::string, std::set<std::string>> BuildFixedSets()
    {
        std::map<std::string, std::set<std::string>> s;
        AddNames(s["rangeType"], EnumNames::RangeTypes);
        AddNames(s["approach"], EnumNames::Approaches);
        AddNames(s["anchor"], EnumNames::Anchors);
        AddNames(s["enemyEffectKind"], EnumNames::EffectKinds);
        AddNames(s["applyTo"], EnumNames::ApplyTos);
        AddNames(s["buff"], EnumNames::BuffTypes);
        AddNames(s["cardEffect"], EnumNames::CardEffectTypes);
        AddNames(s["cardType"], EnumNames::CardTypes);
        AddNames(s["rarity"], EnumNames::CardRarities);
        AddNames(s["condition"], EnumNames::Conditions);
        AddNames(s["addCardDest"], EnumNames::AddCardDestinations);
        AddNames(s["cardSpecialVfx"], EnumNames::CardSpecialVfx);
        AddNames(s["eventOutcome"], EnumNames::EventOutcomeTypes);
        AddNames(s["craftSlot"], EnumNames::CraftSlots);
        AddNames(s["relicRarity"], EnumNames::RelicRarities);
        AddNames(s["relicEffect"], EnumNames::RelicEffects);
        for (auto& n : EnumNames::RelicKindNames()) s["relicKind"].insert(n);
        return s;
    }

    // ==================================================================
    // パスをたどる
    // ==================================================================
    std::vector<std::string> SplitPath(const std::string& path)
    {
        std::vector<std::string> out;
        std::stringstream ss(path);
        std::string tok;
        while (std::getline(ss, tok, '.')) out.push_back(tok);
        return out;
    }

    // 配列の要素の表示名（id があれば id、無ければ番号）
    std::string ElemLabel(const ojson& e, size_t index)
    {
        if (e.is_object() && e.contains("id") && e["id"].is_string()) return e["id"].get<std::string>();
        return std::to_string(index);
    }

    // 行き着いた値（文字列 or 文字列の配列）ごとに visit(value, parentObject, where) を呼ぶ
    template <class Visit>
    void Walk(const ojson& node, const std::vector<std::string>& toks, size_t i,
        const std::string& where, const ojson* parent, Visit& visit)
    {
        if (i == toks.size())
        {
            if (node.is_string()) visit(node.get<std::string>(), parent, where);
            else if (node.is_array())
                for (size_t k = 0; k < node.size(); k++)
                    if (node[k].is_string()) visit(node[k].get<std::string>(), parent, where + "[" + std::to_string(k) + "]");
            return;
        }
        if (!node.is_object()) return;

        const std::string& tok = toks[i];
        if (tok == "*")
        {
            for (auto it = node.begin(); it != node.end(); ++it)
                if (it->is_object()) Walk(*it, toks, i + 1, where + "." + it.key(), &node, visit);
            return;
        }
        if (tok == "#keys")
        {
            for (auto it = node.begin(); it != node.end(); ++it)
                visit(it.key(), &node, where + ".{" + it.key() + "}");
            return;
        }

        bool isArray = tok.size() > 2 && tok.compare(tok.size() - 2, 2, "[]") == 0;
        std::string name = isArray ? tok.substr(0, tok.size() - 2) : tok;
        if (!node.contains(name)) return;
        const ojson& child = node[name];
        std::string here = where.empty() ? name : where + "." + name;

        if (!isArray) { Walk(child, toks, i + 1, here, &node, visit); return; }
        if (!child.is_array()) return;
        for (size_t k = 0; k < child.size(); k++)
        {
            std::string w = here + "[" + ElemLabel(child[k], k) + "]";
            if (i + 1 == toks.size())   // 配列の要素そのものが値（deck[] など）
            {
                if (child[k].is_string()) visit(child[k].get<std::string>(), &node, w);
            }
            else Walk(child[k], toks, i + 1, w, &node, visit);
        }
    }

    // ==================================================================
    // 実行
    // ==================================================================
    struct Checker
    {
        std::map<std::string, JsonFile> files;                    // 読んだファイル
        std::map<std::string, std::set<std::string>> sets;        // 一覧
        std::vector<Problem> problems;

        const ojson* Doc(const std::string& file)
        {
            auto it = files.find(file);
            if (it == files.end())
            {
                JsonFile f;
                if (!f.Load((std::string(DATA_DIR) + file).c_str()))
                {
                    problems.push_back({ Severity::Error, file, "", f.error });
                    f.doc = ojson();   // 読めなかったファイルは空として扱う（同じエラーを何度も出さない）
                }
                it = files.emplace(file, std::move(f)).first;
            }
            return it->second.doc.is_object() ? &it->second.doc : nullptr;
        }

        void BuildDefinitions()
        {
            std::map<std::string, std::map<std::string, int>> counts;   // 一覧 → id → 出現数
            std::map<std::string, std::string> setFile;
            for (auto& d : DEFS)
            {
                const ojson* doc = Doc(d.file);
                if (!doc) continue;
                auto toks = SplitPath(d.path);
                auto visit = [&](const std::string& id, const ojson*, const std::string& where)
                {
                    if (id.empty()) { problems.push_back({ Severity::Error, d.file, where, "id is empty" }); return; }
                    sets[d.set].insert(id);
                    if (++counts[d.set][id] == 2)
                        problems.push_back({ Severity::Error, d.file, where, "duplicate " + std::string(d.set) + " id: " + id });
                };
                Walk(*doc, toks, 0, "", nullptr, visit);
            }
        }

        // "a|b|lit:x,y" のどれかに入っているか
        bool InSet(const std::string& expr, const std::string& value)
        {
            std::stringstream ss(expr);
            std::string part;
            while (std::getline(ss, part, '|'))
            {
                if (part.rfind("lit:", 0) == 0)
                {
                    std::stringstream ls(part.substr(4));
                    std::string lit;
                    while (std::getline(ls, lit, ',')) if (lit == value) return true;
                }
                else if (part == "texture")
                {
                    if (TextureManager::Get(value)) return true;
                }
                else if (part == "cardRef")
                {
                    if (value.rfind("CRAFT:", 0) == 0) return true;   // 合成カードは素材から作られる
                    std::string base = (!value.empty() && value.back() == '+') ? value.substr(0, value.size() - 1) : value;
                    if (sets["card"].count(base)) return true;
                }
                else if (sets.count(part) && sets[part].count(value)) return true;
            }
            return false;
        }

        void CheckReferences()
        {
            for (auto& r : RULES)
            {
                const ojson* doc = Doc(r.file);
                if (!doc) continue;
                auto toks = SplitPath(r.path);
                auto visit = [&](const std::string& value, const ojson* parent, const std::string& where)
                {
                    if (value.empty()) return;
                    if (r.whenKey && (!parent || parent->value(r.whenKey, std::string()) != r.whenValue)) return;
                    if (InSet(r.set, value)) return;
                    problems.push_back({ r.severity, r.file, where, "\"" + value + "\" is not in " + r.set });
                };
                Walk(*doc, toks, 0, "", nullptr, visit);
            }
        }
    };

    // ==================================================================
    // 画面
    // ==================================================================
    struct State
    {
        bool open = false;
        bool ran = false;
        std::vector<Problem> problems;
        bool showErrors = true, showWarnings = true;
        int fileFilter = 0;          // 0 = すべて
        double lastRun = 0.0;
    };
    State s;

    void RunNow()
    {
        s.problems = RefCheck::Run();
        s.ran = true;
        s.lastRun = ImGui::GetTime();
    }
}

std::vector<RefCheck::Problem> RefCheck::Run()
{
    Checker c;
    c.sets = BuildFixedSets();
    c.BuildDefinitions();
    c.CheckReferences();

    // エラーを先、同じ重さならファイル順
    std::stable_sort(c.problems.begin(), c.problems.end(), [](const Problem& a, const Problem& b) {
        if (a.severity != b.severity) return a.severity == Severity::Error;
        return a.file < b.file;
    });
    return c.problems;
}

void RefCheck::Toggle()
{
    s.open = !s.open;
    if (s.open) RunNow();   // 開いた時に毎回チェックする
}

bool RefCheck::IsOpen() { return s.open; }

void RefCheck::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1000, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Reference Check", &s.open))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Run")) RunNow();
    ImGui::SameLine();

    int errors = 0, warnings = 0;
    std::set<std::string> fileSet;
    for (auto& p : s.problems)
    {
        (p.severity == Severity::Error ? errors : warnings)++;
        fileSet.insert(p.file);
    }
    if (errors == 0 && warnings == 0) ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1), u8"問題なし");
    else
    {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1), "%d errors", errors);
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "%d warnings", warnings);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%.0f sec ago)", ImGui::GetTime() - s.lastRun);

    ImGui::Checkbox("errors", &s.showErrors);
    ImGui::SameLine();
    ImGui::Checkbox("warnings", &s.showWarnings);
    ImGui::SameLine();
    std::vector<std::string> files = { "All" };
    files.insert(files.end(), fileSet.begin(), fileSet.end());
    if (s.fileFilter >= (int)files.size()) s.fileFilter = 0;
    ImGui::SetNextItemWidth(170);
    if (ImGui::BeginCombo("file", files[s.fileFilter].c_str()))
    {
        for (int i = 0; i < (int)files.size(); i++)
            if (ImGui::Selectable(files[i].c_str(), s.fileFilter == i)) s.fileFilter = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy"))   // 一覧をテキストでクリップボードへ（メモ・共有用）
    {
        std::string text;
        for (auto& p : s.problems)
            text += std::string(p.severity == Severity::Error ? "[Error] " : "[Warning] ")
                + p.file + "  " + p.where + "  " + p.message + "\n";
        ImGui::SetClipboardText(text.c_str());
    }
    ImGui::TextDisabled(u8"ルールは RefCheck.cpp の DEFS / RULES 表に1行足すだけで増やせる。Warning = テクスチャ未読み込み・セーブデータ");

    if (ImGui::BeginTable("problems", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("file", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("where", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("problem", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableHeadersRow();

        for (auto& p : s.problems)
        {
            bool isErr = p.severity == Severity::Error;
            if (isErr && !s.showErrors) continue;
            if (!isErr && !s.showWarnings) continue;
            if (s.fileFilter > 0 && p.file != files[s.fileFilter]) continue;

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(isErr ? ImVec4(1.0f, 0.45f, 0.45f, 1) : ImVec4(1.0f, 0.8f, 0.3f, 1),
                isErr ? "Error" : "Warning");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.file.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.where.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(p.message.c_str());
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

#else   // Release ではすべて何もしない

std::vector<RefCheck::Problem> RefCheck::Run() { return {}; }
void RefCheck::Toggle() {}
bool RefCheck::IsOpen() { return false; }
void RefCheck::Draw() {}

#endif
