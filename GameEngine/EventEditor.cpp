#include "EventEditor.h"

#ifdef _DEBUG

#include "EditorCommon.h"
#include "EditorHub.h"
#include "EventDataBase.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace EditorCommon;

namespace
{
    const char* EVENT_PATH  = "Assets/Data/events.json";
    const char* BACKUP_PATH = "Assets/Data/events.json.bak";

    // ==================================================================
    // 結果(outcome)の種類の表
    // SceneManager::ApplyOutcomes / EventPickerType / ChoiceEnabled と一致させる
    // ==================================================================
    enum class ParamKind { None, Material, Relic, Card, Battle, PickCard };

    struct OutcomeInfo
    {
        const char* type;
        bool usesValue;
        ParamKind param;
        bool transition;     // 画面が切り替わる（1つの選択肢で最初の1つだけ有効）
        bool picker;         // カード/レリックを選ぶ画面（1つの選択肢で最初の1つだけ有効）
        const char* hint;
    };

    const OutcomeInfo OUTCOMES[] = {
        { "hp",             true,  ParamKind::None,     false, false, u8"HP を増減（+で回復 / -でダメージ）" },
        { "maxHp",          true,  ParamKind::None,     false, false, u8"最大HP を増減（HP も同じだけ増減）" },
        { "gold",           true,  ParamKind::None,     false, false, u8"ゴールド増減。マイナスは支払い（足りないと選べない）" },
        { "relic",          false, ParamKind::Relic,    false, false, u8"レリック入手（param 未指定ならランダム）" },
        { "material",       true,  ParamKind::Material, false, false, u8"素材を value 個（0 なら1個）" },
        { "randomMaterial", true,  ParamKind::None,     false, false, u8"ランダムな素材1個（value = 層、0 = 全層から）" },
        { "addCard",        false, ParamKind::Card,     false, false, u8"デッキにカードを加える（呪いなど）" },
        { "pickCard",       false, ParamKind::PickCard, true,  false, u8"カード報酬画面へ（param: 空=通常 / rare / upgraded）" },
        { "removeCard",     false, ParamKind::None,     false, true,  u8"デッキから1枚選んで削除" },
        { "upgradeCard",    false, ParamKind::None,     false, true,  u8"デッキから1枚選んで強化" },
        { "transformCard",  false, ParamKind::None,     false, true,  u8"デッキから1枚選んで別のカードに変える" },
        { "transformRelic", false, ParamKind::None,     false, true,  u8"レリックを1つ選んで変える（持っていないと選べない）" },
        { "battle",         false, ParamKind::Battle,   true,  false, u8"戦闘へ（param: event / normal / エンカウントid）" },
        { "shop",           false, ParamKind::None,     true,  false, u8"ショップへ" },
        { "treasure",       false, ParamKind::None,     true,  false, u8"財宝へ" },
    };

    const OutcomeInfo* FindOutcome(const std::string& type)
    {
        for (auto& o : OUTCOMES) if (type == o.type) return &o;
        return nullptr;
    }

    const std::vector<std::string>& OutcomeNames()
    {
        static std::vector<std::string> v = [] { std::vector<std::string> r; for (auto& o : OUTCOMES) r.push_back(o.type); return r; }();
        return v;
    }

    std::vector<std::string> ParamOptions(ParamKind k)
    {
        switch (k)
        {
        case ParamKind::Material: return MaterialIds();
        case ParamKind::Relic:    return RelicIds();
        case ParamKind::Card:     return CardIds();
        case ParamKind::PickCard: return { "rare", "upgraded" };
        case ParamKind::Battle:
        {
            std::vector<std::string> v = { "event", "normal" };
            for (auto& id : EncounterIds()) v.push_back(id);
            return v;
        }
        default: return {};
        }
    }

    // ==================================================================
    // 状態
    // ==================================================================
    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;

        JsonFile file;               // events.json そのもの
        int sel = -1;
        int layerFilter = -1;        // -1 = すべて / 0〜3
        int previewGold = 100;       // プレビューで「選べるか」を見る時の所持ゴールド

        std::string status;
    };
    State s;

    ojson& Events() { return s.file.doc["events"]; }

    void LoadAll()
    {
        ReloadIdLists();
        if (!s.file.Load(EVENT_PATH) || !s.file.doc.contains("events") || !s.file.doc["events"].is_array())
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "events.json has no \"events\" array" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        if (s.sel >= (int)Events().size()) s.sel = -1;
        s.status = "Loaded " + std::to_string(Events().size()) + " events";
    }

    // ==================================================================
    // 検証
    // ==================================================================
    void Validate(int index, std::vector<std::string>& err, std::vector<std::string>& warn)
    {
        const ojson& ev = Events()[index];
        std::string id = ev.value("id", std::string());
        if (id.empty()) err.push_back("id is empty");
        for (int i = 0; i < (int)Events().size(); i++)
            if (i != index && !id.empty() && Events()[i].value("id", std::string()) == id)
            { err.push_back("duplicate id: " + id); break; }

        int layer = ev.value("layer", 0);
        if (layer < 0 || layer > 3) err.push_back("layer must be 0-3");

        if (!ev.contains("choices") || !ev["choices"].is_array() || ev["choices"].empty())
        {
            err.push_back("no choices");
            return;
        }

        for (int c = 0; c < (int)ev["choices"].size(); c++)
        {
            const ojson& ch = ev["choices"][c];
            std::string w = "choice " + std::to_string(c);
            if (ch.value("label", std::string()).empty()) err.push_back(w + ": label is empty");
            if (!ch.contains("outcomes")) continue;

            int transitions = 0, pickers = 0;
            for (auto& o : ch["outcomes"])
            {
                std::string type = o.value("type", std::string());
                const OutcomeInfo* info = FindOutcome(type);
                if (!info) { err.push_back(w + ": unknown outcome " + type); continue; }
                if (info->transition) transitions++;
                if (info->picker) pickers++;

                std::string param = o.value("param", std::string());
                if (info->param != ParamKind::None && !param.empty())
                {
                    auto opts = ParamOptions(info->param);
                    if (std::find(opts.begin(), opts.end(), param) == opts.end())
                        err.push_back(w + ": unknown " + type + " param " + param);
                }
                if ((info->param == ParamKind::Card || info->param == ParamKind::Material) && param.empty())
                    err.push_back(w + ": " + type + " needs param");
                int chance = o.value("chance", 100);
                if (chance < 0 || chance > 100) err.push_back(w + ": chance must be 0-100");
            }
            if (transitions > 1) warn.push_back(w + u8": 画面が変わる結果が複数（chance を通った最初の1つだけ有効）");
            if (pickers > 1) warn.push_back(w + u8": 選ぶ画面の結果が複数（最初の1つだけ有効）");
            if (transitions > 0 && pickers > 0) warn.push_back(w + u8": 選ぶ画面と画面遷移が同じ選択肢にある");
        }
    }

    bool Save()
    {
        for (int i = 0; i < (int)Events().size(); i++)
        {
            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            if (!err.empty())
            {
                s.sel = i;
                s.status = "Save blocked: fix the problems of " + Events()[i].value("id", std::string("?"));
                return false;
            }
        }
        if (!s.file.Save(EVENT_PATH, BACKUP_PATH)) { s.status = s.file.error; return false; }
        EventDataBase::Reload(EVENT_PATH);   // イベントは毎フレーム id から引き直しているので、そのまま読み直してよい
        s.dirty = false;
        s.status = "Saved (backup: events.json.bak)";
        return true;
    }

    std::string UniqueId(const std::string& base)
    {
        for (int n = 1;; n++)
        {
            std::string id = base + "_" + std::to_string(n);
            bool used = false;
            for (auto& e : Events()) if (e.value("id", std::string()) == id) { used = true; break; }
            if (!used) return id;
        }
    }

    // 結果1つを短い文にする（プレビュー用）
    std::string Summary(const ojson& o)
    {
        std::string type = o.value("type", std::string("?"));
        int v = o.value("value", 0);
        std::string p = o.value("param", std::string());
        char buf[160];
        if (type == "hp" || type == "maxHp" || type == "gold") snprintf(buf, sizeof(buf), "%s %+d", type.c_str(), v);
        else if (type == "material") snprintf(buf, sizeof(buf), "material %s x%d", p.c_str(), v > 0 ? v : 1);
        else if (type == "randomMaterial") snprintf(buf, sizeof(buf), "randomMaterial (layer %d)", v);
        else if (!p.empty()) snprintf(buf, sizeof(buf), "%s %s", type.c_str(), p.c_str());
        else snprintf(buf, sizeof(buf), "%s", type.c_str());
        std::string out = buf;
        if (o.contains("chance")) out += " (" + std::to_string(o.value("chance", 100)) + "%)";
        return out;
    }

    // ==================================================================
    // 左：イベント一覧
    // ==================================================================
    void DrawList()
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search id / title", filter, sizeof(filter));

        const char* layers[] = { "All", "L0(all)", "L1", "L2", "L3" };
        int li = s.layerFilter + 1;
        ImGui::SetNextItemWidth(100);
        if (ImGui::Combo("##layer", &li, layers, 5)) s.layerFilter = li - 1;

        if (ImGui::Button("New"))
        {
            ojson e;
            e["id"] = UniqueId("new_event");
            e["title"] = u8"新しいイベント";
            e["desc"] = u8"何かが起きた……";
            e["choices"] = ojson::array({
                ojson{ { "label", u8"受け取る（+50G）" }, { "result", u8"ゴールドを得た。" },
                       { "outcomes", ojson::array({ ojson{ { "type", "gold" }, { "value", 50 } } }) } },
                ojson{ { "label", u8"立ち去る" }, { "result", u8"何も起きなかった。" }, { "outcomes", ojson::array() } } });
            e["layer"] = s.layerFilter > 0 ? s.layerFilter : 1;
            Events().push_back(e);
            s.sel = (int)Events().size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(s.sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson e = Events()[s.sel];
            e["id"] = UniqueId(e.value("id", std::string("event")));
            Events().push_back(e);
            s.sel = (int)Events().size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("ConfirmDeleteEvent");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteEvent", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete %s ?", Events()[s.sel].value("id", std::string("?")).c_str());
            if (ImGui::Button("Delete"))
            {
                Events().erase(Events().begin() + s.sel);
                s.sel = -1;
                s.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::BeginChild("EventList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)Events().size(); i++)
        {
            const ojson& e = Events()[i];
            std::string id = e.value("id", std::string("?"));
            std::string title = e.value("title", std::string());
            int layer = e.value("layer", 0);
            if (s.layerFilter >= 0 && layer != s.layerFilter) continue;
            if (filter[0] && id.find(filter) == std::string::npos && title.find(filter) == std::string::npos) continue;

            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            bool fixed = !e.value("random", true);
            char label[200];
            snprintf(label, sizeof(label), "%s%s  %s  L%d%s##%d", err.empty() ? "" : "! ",
                title.c_str(), id.c_str(), layer, fixed ? " [fixed]" : "", i);
            ImU32 col = !err.empty() ? IM_COL32(255, 110, 110, 255)
                : !warn.empty() ? IM_COL32(255, 210, 90, 255) : IM_COL32(225, 225, 225, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::Selectable(label, s.sel == i)) s.sel = i;
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中央：イベントの中身
    // ==================================================================
    void DrawOutcome(ojson& o)
    {
        if (ComboKey("type", o, "type", OutcomeNames(), "", false, 130)) s.dirty = true;
        const OutcomeInfo* info = FindOutcome(o.value("type", std::string()));
        if (!info) return;

        if (info->usesValue)
        {
            ImGui::SameLine();
            if (InputIntKey("value", o, "value", 0, 90)) s.dirty = true;
        }
        if (info->param != ParamKind::None)
        {
            ImGui::SameLine();
            bool optional = (info->param == ParamKind::Relic || info->param == ParamKind::PickCard);
            if (ComboKey("param", o, "param", ParamOptions(info->param), "", optional, 170)) s.dirty = true;
        }
        ImGui::SameLine();
        if (o.contains("chance"))
        {
            if (InputIntKey("chance%", o, "chance", 100, 80)) s.dirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("always")) { o.erase("chance"); s.dirty = true; }   // 省略＝100＝必ず
        }
        else if (ImGui::SmallButton("+ chance")) { o["chance"] = 50; s.dirty = true; }
        ImGui::TextDisabled("%s", info->hint);
    }

    void DrawChoices(ojson& ev)
    {
        if (!ev.contains("choices")) ev["choices"] = ojson::array();
        ojson& choices = ev["choices"];

        int moveFrom = -1, moveTo = -1, removeAt = -1;
        for (int c = 0; c < (int)choices.size(); c++)
        {
            ojson& ch = choices[c];
            ImGui::PushID(c);
            std::string title = "#" + std::to_string(c) + "  " + ch.value("label", std::string());
            if (ImGui::CollapsingHeader((title + "###choice").c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent();
                if (ImGui::SmallButton("Up") && c > 0) { moveFrom = c; moveTo = c - 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && c + 1 < (int)choices.size()) { moveFrom = c; moveTo = c + 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete choice")) removeAt = c;

                if (InputTextKey("label", ch, "label", 420)) s.dirty = true;
                ImGui::TextDisabled(u8"ボタンの文字。効果は（）で書いておくと親切");
                if (InputTextMultilineKey("result", ch, "result", 420, 44)) s.dirty = true;
                ImGui::TextDisabled(u8"選んだ後に出る文");

                if (!ch.contains("outcomes")) ch["outcomes"] = ojson::array();
                ojson& outs = ch["outcomes"];
                int rmO = -1;
                for (int i = 0; i < (int)outs.size(); i++)
                {
                    ImGui::PushID(100 + i);
                    ImGui::Text("[%d]", i);
                    ImGui::SameLine();
                    ImGui::BeginGroup();
                    DrawOutcome(outs[i]);
                    ImGui::EndGroup();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) rmO = i;
                    ImGui::PopID();
                }
                if (rmO >= 0) { outs.erase(outs.begin() + rmO); s.dirty = true; }
                if (ImGui::SmallButton("+ outcome"))
                {
                    outs.push_back(ojson{ { "type", "gold" }, { "value", 10 } });
                    s.dirty = true;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (moveFrom >= 0) { std::swap(choices[moveFrom], choices[moveTo]); s.dirty = true; }
        if (removeAt >= 0) { choices.erase(choices.begin() + removeAt); s.dirty = true; }
        if (ImGui::Button("+ Choice"))
        {
            choices.push_back(ojson{ { "label", u8"新しい選択肢" }, { "result", "" }, { "outcomes", ojson::array() } });
            s.dirty = true;
        }
    }

    void DrawEvent(ojson& ev)
    {
        if (InputTextKey("id", ev, "id", 200)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled(u8"(変えると、コードから id 指定で開いているイベントが見つからなくなる)");
        if (InputTextKey("title", ev, "title", 300)) s.dirty = true;

        int layer = ev.value("layer", 0);
        ImGui::SetNextItemWidth(120);
        if (ImGui::SliderInt("layer", &layer, 0, 3)) { ev["layer"] = layer; s.dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled(u8"0 = どの層でも");
        ImGui::SameLine();
        bool random = ev.value("random", true);
        if (ImGui::Checkbox("random", &random)) { ev["random"] = random; s.dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled(u8"OFF = ランダムには出ない（財宝など、コードから開く用）");

        if (InputTextMultilineKey("desc", ev, "desc", 520, 70)) s.dirty = true;

        ImGui::SeparatorText("choices");
        DrawChoices(ev);
    }

    // ==================================================================
    // 右：プレビュー（選択肢と結果のまとめ）
    // ==================================================================
    void DrawPreview(const ojson& ev)
    {
        ImGui::SeparatorText("Preview");
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.6f, 1.0f), "%s", ev.value("title", std::string()).c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
        ImGui::TextUnformatted(ev.value("desc", std::string()).c_str());
        ImGui::PopTextWrapPos();

        ImGui::SetNextItemWidth(100);
        ImGui::InputInt(u8"所持ゴールド(仮)", &s.previewGold);

        if (!ev.contains("choices")) return;
        for (auto& ch : ev["choices"])
        {
            // 必要なゴールド（SceneManager::ChoiceEnabled と同じ計算）
            int need = 0;
            if (ch.contains("outcomes"))
                for (auto& o : ch["outcomes"])
                    if (o.value("type", std::string()) == "gold" && o.value("value", 0) < 0) need += -o.value("value", 0);
            bool enabled = s.previewGold >= need;

            ImGui::Separator();
            ImGui::TextColored(enabled ? ImVec4(1, 1, 1, 1) : ImVec4(0.45f, 0.45f, 0.45f, 1), "[ %s ]",
                ch.value("label", std::string()).c_str());
            if (!enabled) { ImGui::SameLine(); ImGui::TextDisabled(u8"(%dG 必要)", need); }
            if (ch.contains("outcomes"))
                for (auto& o : ch["outcomes"]) ImGui::BulletText("%s", Summary(o).c_str());
            ImGui::TextColored(ImVec4(0.7f, 1.0f, 0.7f, 1), "-> %s", ch.value("result", std::string()).c_str());
        }
        ImGui::Separator();
        ImGui::TextDisabled(u8"見た目の確認は「Save & Open in Game」で本物のイベント画面を開く");
        ImGui::TextDisabled(u8"（選ぶと結果は実際にセーブデータへ反映されます）");
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void EventEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool EventEditor::IsOpen() { return s.open; }

void EventEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1250, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Event Editor", &s.open))
    {
        ImGui::End();
        return;
    }

    if (!s.loaded)
    {
        ImGui::TextUnformatted(s.status.c_str());
        if (ImGui::Button("Retry")) LoadAll();
        ImGui::End();
        return;
    }

    if (ImGui::Button("Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Revert")) LoadAll();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.sel < 0);
    if (ImGui::Button("Save & Open in Game") && Save())
        EditorHub::RequestOpenEvent(Events()[s.sel].value("id", std::string()));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (s.dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* unsaved");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", s.status.c_str());
    ImGui::Separator();

    ImGui::BeginChild("Left", ImVec2(300, 0), ImGuiChildFlags_ResizeX);
    DrawList();
    ImGui::EndChild();
    ImGui::SameLine();

    float previewW = 400.0f;
    ImGui::BeginChild("Center", ImVec2(ImGui::GetContentRegionAvail().x - previewW - 8, 0), ImGuiChildFlags_Borders);
    if (s.sel >= 0 && s.sel < (int)Events().size())
    {
        std::vector<std::string> err, warn;
        Validate(s.sel, err, warn);
        DrawProblems(err);
        for (auto& w : warn) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "? %s", w.c_str());
        DrawEvent(Events()[s.sel]);
    }
    else ImGui::TextDisabled("Select an event on the left");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Right", ImVec2(0, 0));
    if (s.sel >= 0 && s.sel < (int)Events().size()) DrawPreview(Events()[s.sel]);
    ImGui::EndChild();

    ImGui::End();
}

#else   // Release ではすべて何もしない

void EventEditor::Toggle() {}
bool EventEditor::IsOpen() { return false; }
void EventEditor::Draw() {}

#endif
