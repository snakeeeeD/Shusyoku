#include "RelicEditor.h"

#ifdef _DEBUG

#include "EditorCommon.h"
#include "EnumNames.h"
#include "PlayerDataManager.h"
#include "RelicManager.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace EditorCommon;

namespace
{
    const char* RELIC_PATH  = "Assets/Data/relics.json";
    const char* BACKUP_PATH = "Assets/Data/relics.json.bak";

    // レアリティごとの入手経路（RelicManager::RandomDrop / ShopPool / RandomUnowned と一致させる）
    const char* RarityHint(const std::string& r)
    {
        if (r == "common")   return u8"エリート撃破などのランダム入手（重み65）・ショップ";
        if (r == "uncommon") return u8"ランダム入手（重み27）・ショップ";
        if (r == "rare")     return u8"ランダム入手（重み8）・ショップ";
        if (r == "shop")     return u8"ショップだけ（ランダム入手には出ない）";
        if (r == "boss")     return u8"ショップ・ランダム入手に出ない（ボス報酬用）";
        if (r == "event")    return u8"イベントの relic 結果（param 未指定）から出る。ショップには出ない";
        if (r == "starter")  return u8"初期所持用（ランダム入手には出ない）";
        return "";
    }

    // バーの枠の色（SceneManager::DrawRelicBar の rarColor と同じ）
    ImVec4 RarityColor(const std::string& r)
    {
        if (r == "uncommon") return ImVec4(0.2f, 0.6f, 0.9f, 1);
        if (r == "rare")     return ImVec4(0.95f, 0.8f, 0.2f, 1);
        if (r == "boss")     return ImVec4(0.9f, 0.3f, 0.3f, 1);
        if (r == "event")    return ImVec4(0.3f, 0.8f, 0.4f, 1);
        if (r == "shop")     return ImVec4(0.7f, 0.4f, 0.9f, 1);
        return ImVec4(0.55f, 0.55f, 0.55f, 1);
    }

    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;

        JsonFile file;          // relics.json そのもの
        int sel = -1;
        int rarityFilter = -1;  // -1 = すべて

        std::string status;
    };
    State s;

    ojson& Relics() { return s.file.doc["relics"]; }

    const std::vector<std::string>& KindNames()    { static auto v = EnumNames::RelicKindNames();                 return v; }
    const std::vector<std::string>& RarityNames()  { static auto v = EnumNames::Names(EnumNames::RelicRarities);  return v; }
    const std::vector<std::string>& EffectNames()  { static auto v = EnumNames::Names(EnumNames::RelicEffects);   return v; }

    void LoadAll()
    {
        if (!s.file.Load(RELIC_PATH) || !s.file.doc.contains("relics") || !s.file.doc["relics"].is_array())
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "relics.json has no \"relics\" array" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        if (s.sel >= (int)Relics().size()) s.sel = -1;
        s.status = "Loaded " + std::to_string(Relics().size()) + " relics";
    }

    // 説明文の中の数字（半角）を全部取り出す
    std::vector<int> NumbersIn(const std::string& text)
    {
        std::vector<int> out;
        int cur = -1;
        for (char ch : text)
        {
            if (ch >= '0' && ch <= '9') cur = (cur < 0 ? 0 : cur * 10) + (ch - '0');
            else if (cur >= 0) { out.push_back(cur); cur = -1; }
        }
        if (cur >= 0) out.push_back(cur);
        return out;
    }

    void Validate(int index, std::vector<std::string>& err, std::vector<std::string>& warn)
    {
        const ojson& r = Relics()[index];
        std::string id = r.value("id", std::string());
        if (!r.contains("id") || id.empty()) err.push_back("id is empty");   // ゲームは r["id"] で読む
        for (int i = 0; i < (int)Relics().size(); i++)
            if (i != index && !id.empty() && Relics()[i].value("id", std::string()) == id)
            { err.push_back("duplicate id: " + id); break; }

        std::string kind = r.value("kind", std::string());
        const EnumNames::RelicKindInfo* info = EnumNames::FindRelicKind(kind);
        if (!info) err.push_back("unknown kind " + kind + u8"（コードに無い kind は何も起きない）");
        std::string rarity = r.value("rarity", std::string("common"));
        if (!EnumNames::Contains(EnumNames::RelicRarities, rarity)) err.push_back("unknown rarity " + rarity);
        if (r.value("name", std::string()).empty()) warn.push_back(u8"name が空");

        int value = r.value("value", 0), count = r.value("count", 0);
        if (info && info->counting)
        {
            if (count <= 0) err.push_back(u8"カウント系は count が1以上必要（0だと発動しない）");
            std::string effect = r.value("effect", std::string("block"));
            if (!EnumNames::Contains(EnumNames::RelicEffects, effect)) err.push_back("unknown effect " + effect);
        }
        else if (info)
        {
            if (r.contains("count") || r.contains("effect") || r.contains("perTurn"))
                warn.push_back(u8"count / effect / perTurn はカウント系の kind でしか使われない");
        }

        // 説明文の数字と値の食い違い（説明に数字がある時だけ）
        auto nums = NumbersIn(r.value("desc", std::string()));
        bool usesValue = info && kind != "poisonSpread" && kind != "rewardRare";
        if (!nums.empty() && usesValue && std::find(nums.begin(), nums.end(), value) == nums.end())
            warn.push_back(u8"説明文に value(" + std::to_string(value) + u8") の数字が無い（説明と実際の値が違う？）");
        if (!nums.empty() && info && info->counting && std::find(nums.begin(), nums.end(), count) == nums.end())
            warn.push_back(u8"説明文に count(" + std::to_string(count) + u8") の数字が無い");
    }

    bool Save()
    {
        for (int i = 0; i < (int)Relics().size(); i++)
        {
            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            if (!err.empty())
            {
                s.sel = i;
                s.status = "Save blocked: fix the problems of " + Relics()[i].value("id", std::string("?"));
                return false;
            }
        }
        if (!s.file.Save(RELIC_PATH, BACKUP_PATH)) { s.status = s.file.error; return false; }
        RelicManager::Reload(RELIC_PATH);   // レリックは毎回 id から引き直しているので、そのまま読み直してよい
        ReloadIdLists();
        s.dirty = false;
        s.status = "Saved (backup: relics.json.bak)";
        return true;
    }

    std::string UniqueId(const std::string& base)
    {
        for (int n = 1;; n++)
        {
            std::string id = base + "_" + std::to_string(n);
            bool used = false;
            for (auto& r : Relics()) if (r.value("id", std::string()) == id) { used = true; break; }
            if (!used) return id;
        }
    }

    bool Owned(const std::string& id)
    {
        auto& v = PlayerDataManager::GetData().relics;
        return std::find(v.begin(), v.end(), id) != v.end();
    }

    // ==================================================================
    // 左：一覧
    // ==================================================================
    void DrawList()
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search id / name / kind", filter, sizeof(filter));

        std::vector<std::string> rars = { "All" };
        for (const char* r : EnumNames::RelicRarities) rars.push_back(r);
        int ri = s.rarityFilter + 1;
        ImGui::SetNextItemWidth(110);
        if (ImGui::BeginCombo("##rarity", rars[ri].c_str()))
        {
            for (int i = 0; i < (int)rars.size(); i++)
                if (ImGui::Selectable(rars[i].c_str(), ri == i)) s.rarityFilter = i - 1;
            ImGui::EndCombo();
        }

        if (ImGui::Button("New"))
        {
            ojson r;
            r["id"] = UniqueId("new_relic");
            r["name"] = u8"新しいレリック";
            r["kind"] = "startBlock";
            r["value"] = 5;
            r["rarity"] = "common";
            r["desc"] = u8"戦闘開始時、ブロック+5";
            Relics().push_back(r);
            s.sel = (int)Relics().size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(s.sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson r = Relics()[s.sel];
            r["id"] = UniqueId(r.value("id", std::string("relic")));
            Relics().push_back(r);
            s.sel = (int)Relics().size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("ConfirmDeleteRelic");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteRelic", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete %s ?", Relics()[s.sel].value("id", std::string("?")).c_str());
            ImGui::TextDisabled(u8"イベントや所持品から参照されていると見つからなくなる（Check References で確認）");
            if (ImGui::Button("Delete"))
            {
                Relics().erase(Relics().begin() + s.sel);
                s.sel = -1;
                s.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::BeginChild("RelicList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)Relics().size(); i++)
        {
            const ojson& r = Relics()[i];
            std::string id = r.value("id", std::string("?"));
            std::string name = r.value("name", std::string());
            std::string kind = r.value("kind", std::string());
            std::string rarity = r.value("rarity", std::string("common"));
            if (s.rarityFilter >= 0 && rarity != EnumNames::RelicRarities[s.rarityFilter]) continue;
            if (filter[0] && id.find(filter) == std::string::npos && name.find(filter) == std::string::npos
                && kind.find(filter) == std::string::npos) continue;

            std::vector<std::string> err, warn;
            Validate(i, err, warn);

            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::Selectable("##pick", s.sel == i, 0, ImVec2(0, 20));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(ImVec2(p.x + 2, p.y + 3), ImVec2(p.x + 16, p.y + 17), ImGui::GetColorU32(RarityColor(rarity)));
            ImU32 col = !err.empty() ? IM_COL32(255, 110, 110, 255) : !warn.empty() ? IM_COL32(255, 210, 90, 255) : IM_COL32(225, 225, 225, 255);
            char label[200];
            snprintf(label, sizeof(label), "%s%s  %s%s", err.empty() ? "" : "! ", name.c_str(), kind.c_str(), Owned(id) ? "  [owned]" : "");
            dl->AddText(ImVec2(p.x + 22, p.y + 2), col, label);
            if (clicked) s.sel = i;
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中央：中身
    // ==================================================================
    void DrawRelic(ojson& r)
    {
        std::vector<std::string> err, warn;
        Validate(s.sel, err, warn);
        DrawProblems(err);
        for (auto& w : warn) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "? %s", w.c_str());

        if (InputTextKey("id", r, "id", 200)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled(u8"(変えるとイベント・所持品の参照が切れる)");
        if (InputTextKey("name", r, "name", 200)) s.dirty = true;

        if (ComboKey("rarity", r, "rarity", RarityNames(), "common", false, 120)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled("%s", RarityHint(r.value("rarity", std::string("common"))));

        ImGui::SeparatorText(u8"効果");
        if (ComboKey("kind", r, "kind", KindNames(), "", false, 150)) s.dirty = true;
        const EnumNames::RelicKindInfo* info = EnumNames::FindRelicKind(r.value("kind", std::string()));
        if (info) { ImGui::SameLine(); ImGui::TextDisabled("%s", info->hint); }
        if (InputIntKey("value", r, "value", 0, 90)) s.dirty = true;

        if (info && info->counting)
        {
            ImGui::SameLine();
            if (InputIntKey("count", r, "count", 0, 90)) s.dirty = true;
            if (ComboKey("effect", r, "effect", EffectNames(), "block", false, 100)) s.dirty = true;
            ImGui::SameLine();
            if (CheckboxKey(u8"perTurn（毎ターン数え直す）", r, "perTurn")) s.dirty = true;
            std::string effect = r.value("effect", std::string("block"));
            ImGui::TextDisabled(u8"→ %s、%s を %d", info->hint, effect == "energy" ? u8"エナジー" : u8"ブロック", r.value("value", 0));
        }

        ImGui::SeparatorText(u8"説明文");
        if (InputTextMultilineKey("desc", r, "desc", 460, 50)) s.dirty = true;
        ImGui::TextDisabled(u8"説明文は手書き（数字が value / count と合っているかは自動でチェック）");

        // バーでの見た目（色つきの枠＋名前＋説明）
        ImGui::SeparatorText(u8"見た目（レリック帯のホバー説明）");
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 frame = ImGui::GetColorU32(RarityColor(r.value("rarity", std::string("common"))));
        dl->AddRectFilled(p, ImVec2(p.x + 48, p.y + 34), frame, 4.0f);
        dl->AddRectFilled(ImVec2(p.x + 3, p.y + 3), ImVec2(p.x + 45, p.y + 31), IM_COL32(20, 20, 28, 255), 3.0f);
        ImGui::Dummy(ImVec2(52, 34));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextColored(RarityColor(r.value("rarity", std::string("common"))), "%s", r.value("name", std::string()).c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400.0f);
        ImGui::TextUnformatted(r.value("desc", std::string()).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();

        // ---- 試す ----
        ImGui::SeparatorText(u8"試す");
        std::string id = r.value("id", std::string());
        bool owned = Owned(id);
        ImGui::Text(owned ? u8"所持中" : u8"未所持");
        ImGui::SameLine();
        if (!owned && ImGui::Button("Save & Give") && Save())
            PlayerDataManager::AddRelic(id);   // 取得時の効果（maxHp / steps）も普通に入手した時と同じく適用される
        if (owned && ImGui::Button("Remove"))
            PlayerDataManager::RemoveRelic(id);
        ImGui::TextDisabled(u8"戦闘開始時の効果は、次の戦闘（または Battle タブの Reload & Reset）から");
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void RelicEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool RelicEditor::IsOpen() { return s.open; }

void RelicEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1050, 640), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Relic Editor", &s.open))
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
    if (s.dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* unsaved");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", s.status.c_str());
    ImGui::Separator();

    ImGui::BeginChild("Left", ImVec2(340, 0), ImGuiChildFlags_ResizeX);
    DrawList();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Center", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (s.sel >= 0 && s.sel < (int)Relics().size()) DrawRelic(Relics()[s.sel]);
    else ImGui::TextDisabled("Select a relic on the left");
    ImGui::EndChild();

    ImGui::End();
}

#else   // Release ではすべて何もしない

void RelicEditor::Toggle() {}
bool RelicEditor::IsOpen() { return false; }
void RelicEditor::Draw() {}

#endif
