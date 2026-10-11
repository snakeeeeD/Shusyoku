#include "CraftEditor.h"

#ifdef _DEBUG

#include "CardDataBase.h"
#include "EditorCardOverlay.h"
#include "EditorCommon.h"
#include "EditorHub.h"
#include "EnumNames.h"
#include "MaterialDataBase.h"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace EditorCommon;

namespace
{
    const char* MAT_PATH    = "Assets/Data/materials.json";
    const char* BACKUP_PATH = "Assets/Data/materials.json.bak";
    constexpr int MAX_SLOTS = 6;

    // ==================================================================
    // 素材の効果の付き方（CardDataBase.cpp の applyEntry と一致させる）
    // ==================================================================
    struct SlotInfo
    {
        const char* slot;
        bool usesValue;
        bool usesType;       // type（効果の種類）を指定する
        bool usesDuration;
        const char* hint;
    };

    const SlotInfo SLOTS[] = {
        { "amplifyMain", true,  false, false, u8"メイン効果の値 +value" },
        { "hits",        true,  false, false, u8"連撃数 +value" },
        { "main",        true,  true,  true,  u8"メイン効果を type/value で置き換える" },
        { "sub",         true,  true,  true,  u8"サブ効果（空きなら追加、同じ種類なら加算）" },
        { "onHit",       true,  true,  true,  u8"命中時の効果（2枠まで。同じ種類なら加算）" },
        { "pierce",      false, false, false, u8"貫通にする" },
        { "dash",        true,  false, false, u8"移動攻撃にする（近接なら十字に）・射程 +value" },
        { "range",       true,  false, false, u8"範囲化（近接なら周囲1マス）・射程 +value" },
        { "exhaust",     true,  false, false, u8"廃棄にする代わりにメイン +value" },
        { "reckless",    true,  false, true,  u8"メイン +value、自分に duration ダメージ" },
        { "none",        false, false, false, u8"効果なし（コストとタグだけ）" },
        { "onArrival",   false, false, false, u8"未実装（効果なし）" },
    };

    const SlotInfo* FindSlot(const std::string& s)
    {
        for (auto& x : SLOTS) if (s == x.slot) return &x;
        return nullptr;
    }

    // ==================================================================
    // 状態
    // ==================================================================
    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;

        JsonFile file;          // materials.json そのもの
        int tab = 0;            // 0:素材 1:土台
        int selMat = -1, selBase = -1;

        // 合成プレビュー
        std::string pvBase;
        std::string pvMats[MAX_SLOTS];
        bool overlay = true;
        bool overlayLeft = true;   // カード編集のプレビュー（右）と重ならないよう既定は左

        std::string pvKey;      // 前回プレビューを作った時の中身（変わった時だけ作り直す）
        bool pvOk = false;
        std::string pvError;
        std::string pvId;       // "CRAFT:土台|素材|素材"
        CardData pvCard;

        std::string status;
    };
    State s;

    ojson& Bases() { return s.file.doc["bases"]; }
    ojson& Mats() { return s.file.doc["materials"]; }

    const std::vector<std::string>& TypeNames()   { static auto v = EnumNames::Names(EnumNames::CardTypes);       return v; }
    const std::vector<std::string>& RangeNames()  { static auto v = EnumNames::Names(EnumNames::RangeTypes);      return v; }
    const std::vector<std::string>& EffectNames() { static auto v = EnumNames::Names(EnumNames::CardEffectTypes); return v; }
    const std::vector<std::string>& BuffNames()   { static auto v = EnumNames::Names(EnumNames::BuffTypes);       return v; }
    const std::vector<std::string>& SlotNames()   { static auto v = EnumNames::Names(EnumNames::CraftSlots);      return v; }
    std::vector<std::string> VfxNames()
    {
        std::vector<std::string> v = EffectIds();
        for (const char* n : EnumNames::CardSpecialVfx) v.push_back(n);
        return v;
    }

    std::string ToUtf8(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
        return s;
    }

    void LoadAll()
    {
        ReloadIdLists();
        if (!s.file.Load(MAT_PATH) || !s.file.doc.contains("bases") || !s.file.doc.contains("materials"))
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "materials.json needs \"bases\" and \"materials\"" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        s.pvKey.clear();
        if (s.selMat >= (int)Mats().size()) s.selMat = -1;
        if (s.selBase >= (int)Bases().size()) s.selBase = -1;
        s.status = "Loaded " + std::to_string(Bases().size()) + " bases, " + std::to_string(Mats().size()) + " materials";
    }

    const ojson* FindById(ojson& list, const std::string& id)
    {
        for (auto& e : list) if (e.value("id", std::string()) == id) return &e;
        return nullptr;
    }

    // ==================================================================
    // 検証
    // ==================================================================
    bool InList(const std::vector<std::string>& list, const std::string& v)
    {
        return std::find(list.begin(), list.end(), v) != list.end();
    }

    // 土台と素材は id を共有する（所持品の materials に同じ表で入るため）
    int CountId(const std::string& id)
    {
        int n = 0;
        for (auto& b : Bases()) if (b.value("id", std::string()) == id) n++;
        for (auto& m : Mats()) if (m.value("id", std::string()) == id) n++;
        return n;
    }

    void ValidateBase(const ojson& b, std::vector<std::string>& err)
    {
        // ゲームが b["..."] で直接読む項目（無いと assert で落ちる）
        for (const char* key : { "id", "type", "rangeType" })
            if (!b.contains(key)) err.push_back(std::string(key) + " is missing");
        std::string id = b.value("id", std::string());
        if (id.empty()) err.push_back("id is empty");
        else if (CountId(id) > 1) err.push_back("duplicate id (bases + materials): " + id);
        if (!EnumNames::Contains(EnumNames::CardTypes, b.value("type", std::string()))) err.push_back("unknown type " + b.value("type", std::string()));
        if (!EnumNames::Contains(EnumNames::RangeTypes, b.value("rangeType", std::string()))) err.push_back("unknown rangeType " + b.value("rangeType", std::string()));
        if (!EnumNames::Contains(EnumNames::CardEffectTypes, b.value("mainType", std::string("None")))) err.push_back("unknown mainType " + b.value("mainType", std::string()));
        std::string vfx = b.value("vfx", std::string());
        if (!vfx.empty() && !InList(VfxNames(), vfx)) err.push_back("unknown vfx " + vfx);
        int slots = b.value("modSlots", 2);
        if (slots < 0 || slots > MAX_SLOTS) err.push_back("modSlots must be 0-" + std::to_string(MAX_SLOTS));
    }

    void ValidateMat(const ojson& m, std::vector<std::string>& err, std::vector<std::string>& warn)
    {
        if (!m.contains("id")) err.push_back("id is missing");
        std::string id = m.value("id", std::string());
        if (id.empty()) err.push_back("id is empty");
        else if (CountId(id) > 1) err.push_back("duplicate id (bases + materials): " + id);

        bool any = false;
        for (const char* key : EnumNames::CraftEntryKeys)
        {
            if (!m.contains(key)) continue;
            any = true;
            const ojson& e = m[key];
            std::string w = key;
            std::string slot = e.value("slot", std::string("none"));
            const SlotInfo* info = FindSlot(slot);
            if (!info) { err.push_back(w + ": unknown slot " + slot); continue; }
            std::string type = e.value("type", std::string());
            if (info->usesType && type.empty()) err.push_back(w + ": slot " + slot + " needs type");
            if (!type.empty() && !EnumNames::Contains(EnumNames::CardEffectTypes, type)) err.push_back(w + ": unknown type " + type);
            std::string bt = e.value("buffType", std::string());
            if (!bt.empty() && !EnumNames::Contains(EnumNames::BuffTypes, bt)) err.push_back(w + ": unknown buffType " + bt);
            if ((type == "ApplyBuff" || type == "ApplyDebuff") && bt.empty()) err.push_back(w + ": " + type + " needs buffType");
            std::string vfx = e.value("vfx", std::string());
            if (!vfx.empty() && !InList(VfxNames(), vfx)) err.push_back(w + ": unknown vfx " + vfx);
        }
        if (!any) warn.push_back(u8"効果が1つもない（合成してもコストとタグだけ。RandomId の候補からも外れる）");
    }

    bool Save()
    {
        for (int i = 0; i < (int)Bases().size(); i++)
        {
            std::vector<std::string> err;
            ValidateBase(Bases()[i], err);
            if (!err.empty()) { s.tab = 1; s.selBase = i; s.status = "Save blocked: base " + Bases()[i].value("id", std::string("?")); return false; }
        }
        for (int i = 0; i < (int)Mats().size(); i++)
        {
            std::vector<std::string> err, warn;
            ValidateMat(Mats()[i], err, warn);
            if (!err.empty()) { s.tab = 0; s.selMat = i; s.status = "Save blocked: material " + Mats()[i].value("id", std::string("?")); return false; }
        }
        if (!s.file.Save(MAT_PATH, BACKUP_PATH)) { s.status = s.file.error; return false; }
        MaterialDataBase::Reload(MAT_PATH);   // 土台・素材は毎回 id から引き直しているので、そのまま読み直してよい
        ReloadIdLists();
        EditorHub::RequestCardReload("");     // 作り済みの合成カードは CardDataBase に残っているので、手札ごと作り直してもらう
        s.dirty = false;
        s.status = "Saved (backup: materials.json.bak)";
        return true;
    }

    std::string UniqueId(const std::string& base)
    {
        for (int n = 1;; n++)
        {
            std::string id = base + "_" + std::to_string(n);
            if (CountId(id) == 0) return id;
        }
    }

    // ==================================================================
    // 合成プレビュー（ゲームと同じ計算で CardData を作る）
    // ==================================================================
    void UpdatePreview()
    {
        const ojson* base = s.pvBase.empty() ? nullptr : FindById(Bases(), s.pvBase);
        std::string key = base ? base->dump() : "";
        std::vector<const ojson*> mats;
        for (auto& id : s.pvMats)
        {
            if (id.empty()) continue;
            const ojson* m = FindById(Mats(), id);
            if (m) { mats.push_back(m); key += m->dump(); }
        }
        if (key == s.pvKey) return;   // 変わっていなければ作り直さない
        s.pvKey = key;
        s.pvOk = false;
        s.pvError.clear();

        if (!base) { s.pvError = u8"土台を選んでください"; return; }

        // ゲームの読み込みは項目が無いと assert で落ちるので、検証を通ったものだけ読む
        std::vector<std::string> err, warn;
        ValidateBase(*base, err);
        for (auto* m : mats) ValidateMat(*m, err, warn);
        if (!err.empty()) { s.pvError = u8"赤い「!」の問題を直すとプレビューが出ます"; return; }

        BaseDef bd;
        if (!MaterialDataBase::ParseBaseText(base->dump(), bd)) { s.pvError = "base parse error"; return; }
        std::vector<MaterialDef> defs(mats.size());   // 先に全部作ってからポインタを取る（作りながらだと並びが動く）
        for (size_t i = 0; i < mats.size(); i++)
            if (!MaterialDataBase::ParseMaterialText(mats[i]->dump(), defs[i])) { s.pvError = "material parse error"; return; }
        std::vector<const MaterialDef*> ptrs;
        s.pvId = "CRAFT:" + bd.id;
        for (auto& d : defs) { ptrs.push_back(&d); s.pvId += "|" + d.id; }

        s.pvCard = CardDataBase::BuildCraftedFrom(s.pvId, bd, ptrs);
        s.pvOk = true;
    }

    void DrawPreviewPanel()
    {
        ImGui::SeparatorText(u8"合成プレビュー");
        std::vector<std::string> baseIds, matIds;
        for (auto& b : Bases()) baseIds.push_back(b.value("id", std::string()));
        for (auto& m : Mats()) matIds.push_back(m.value("id", std::string()));

        ojson tmp = { { "v", s.pvBase } };
        if (ComboKey(u8"土台", tmp, "v", baseIds, "", false, 170)) s.pvBase = tmp["v"].get<std::string>();

        const ojson* base = s.pvBase.empty() ? nullptr : FindById(Bases(), s.pvBase);
        int slots = base ? (std::min)(MAX_SLOTS, (std::max)(0, base->value("modSlots", 2))) : 0;   // () で windows.h の min/max マクロを避ける
        std::string baseType = base ? base->value("type", std::string()) : "";
        for (int i = 0; i < slots; i++)
        {
            ImGui::PushID(i);
            ojson t = s.pvMats[i].empty() ? ojson::object() : ojson{ { "v", s.pvMats[i] } };
            char label[32]; snprintf(label, sizeof(label), u8"素材 %d", i + 1);
            if (ComboKey(label, t, "v", matIds, "", true, 170)) s.pvMats[i] = t.value("v", std::string());

            // この土台の種類で、どの効果が使われるか（MaterialDef::entryFor と同じ順：種類 → all）
            if (const ojson* m = s.pvMats[i].empty() ? nullptr : FindById(Mats(), s.pvMats[i]))
            {
                const char* used = m->contains(baseType) ? baseType.c_str() : m->contains("all") ? "all" : nullptr;
                ImGui::SameLine();
                if (used) ImGui::TextDisabled("-> %s: %s", used, (*m)[used].value("slot", std::string("none")).c_str());
                else      ImGui::TextDisabled(u8"-> この種類では効果なし");
            }
            ImGui::PopID();
        }
        for (int i = slots; i < MAX_SLOTS; i++) s.pvMats[i].clear();   // 枠より後ろは使わない
        if (ImGui::SmallButton("clear")) for (auto& m : s.pvMats) m.clear();

        UpdatePreview();
        if (!s.pvOk) { ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1), "%s", s.pvError.c_str()); return; }

        ImGui::Text("%s", ToUtf8(s.pvCard.name).c_str());
        ImGui::Text("cost %d   %s", s.pvCard.cost, s.pvId.c_str());
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 330.0f);
        ImGui::TextDisabled("%s", ToUtf8(s.pvCard.description).c_str());
        ImGui::PopTextWrapPos();

        ImGui::Checkbox(u8"ゲーム画面に表示", &s.overlay);
        ImGui::SameLine();
        ImGui::Checkbox(u8"左側に出す", &s.overlayLeft);
        if (ImGui::Button("Save & Add to Hand") && Save())
            EditorHub::RequestCardReload(s.pvId);   // 合成カードは id から作られるので、そのまま手札に足せる
        ImGui::TextDisabled(u8"戦闘中に Battle タブを開くと手札に入る");
    }

    // ==================================================================
    // 一覧（素材・土台で共通）
    // ==================================================================
    void DrawList(ojson& list, int& sel, bool isBase)
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search id / name", filter, sizeof(filter));

        if (ImGui::Button("New"))
        {
            ojson e = isBase
                ? ojson::parse(u8R"({"name":"新しい土台","desc":"","type":"Attack","layer":1,"rangeType":"Adjacent",
                    "range":1,"cost":1,"buyPrice":120,"sellPrice":20,"modSlots":2,"mainType":"Damage","mainValue":3,"vfx":"slash"})")
                : ojson::parse(u8R"({"name":"新しい素材","tag":"新","layer":1,"desc":"","cost":0,"buyPrice":120,"sellPrice":10,
                    "Attack":{"slot":"amplifyMain","value":2}})");
            ojson withId;
            withId["id"] = UniqueId(isBase ? "core_new" : "mat_new");   // id を先頭に
            for (auto it = e.begin(); it != e.end(); ++it) withId[it.key()] = it.value();
            list.push_back(withId);
            sel = (int)list.size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson e = list[sel];
            e["id"] = UniqueId(e.value("id", std::string(isBase ? "core" : "mat")));
            list.push_back(e);
            sel = (int)list.size() - 1;
            s.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("ConfirmDeleteCraft");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDeleteCraft", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete %s ?", list[sel].value("id", std::string("?")).c_str());
            ImGui::TextDisabled(u8"敵のドロップ・イベント・所持品から参照されていると見つからなくなる（Check References で確認）");
            if (ImGui::Button("Delete"))
            {
                list.erase(list.begin() + sel);
                sel = -1;
                s.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::BeginChild("List", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)list.size(); i++)
        {
            const ojson& e = list[i];
            std::string id = e.value("id", std::string("?"));
            std::string name = e.value("name", std::string());
            if (filter[0] && id.find(filter) == std::string::npos && name.find(filter) == std::string::npos) continue;

            std::vector<std::string> err, warn;
            if (isBase) ValidateBase(e, err); else ValidateMat(e, err, warn);
            char label[200];
            snprintf(label, sizeof(label), "%s%s  %s  L%d##%d", err.empty() ? "" : "! ", name.c_str(), id.c_str(), e.value("layer", 0), i);
            ImU32 col = !err.empty() ? IM_COL32(255, 110, 110, 255) : !warn.empty() ? IM_COL32(255, 210, 90, 255) : IM_COL32(225, 225, 225, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            if (ImGui::Selectable(label, sel == i)) sel = i;
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中身の編集
    // ==================================================================
    void DrawCommon(ojson& e)
    {
        if (InputTextKey("id", e, "id", 200)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled(u8"(変えると所持品・ドロップ・イベントの参照が切れる)");
        if (InputTextKey("name", e, "name", 200)) s.dirty = true;
        if (InputTextMultilineKey("desc", e, "desc", 460, 44)) s.dirty = true;
        int layer = e.value("layer", 0);
        ImGui::SetNextItemWidth(110);
        if (ImGui::SliderInt("layer", &layer, 0, 3)) { e["layer"] = layer; s.dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled(u8"0 = 全層");
        if (InputIntKey("cost", e, "cost", 0, 80)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("buyPrice", e, "buyPrice", 0, 90)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("sellPrice", e, "sellPrice", 0, 90)) s.dirty = true;
    }

    void DrawBase(ojson& b)
    {
        std::vector<std::string> err;
        ValidateBase(b, err);
        DrawProblems(err);

        DrawCommon(b);
        ImGui::SeparatorText(u8"合成カードの元");
        if (ComboKey("type", b, "type", TypeNames(), "Attack", false, 100)) s.dirty = true;
        ImGui::SameLine();
        if (ComboKey("rangeType", b, "rangeType", RangeNames(), "Adjacent", false, 120)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("range", b, "range", 0, 80)) s.dirty = true;
        if (ComboKey("mainType", b, "mainType", EffectNames(), "None", false, 130)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("mainValue", b, "mainValue", 0, 80)) s.dirty = true;
        int slots = b.value("modSlots", 2);
        ImGui::SetNextItemWidth(110);
        if (ImGui::SliderInt("modSlots", &slots, 0, MAX_SLOTS)) { b["modSlots"] = slots; s.dirty = true; }
        ImGui::SameLine(); ImGui::TextDisabled(u8"素材を付けられる数");
        if (ComboKey("vfx", b, "vfx", VfxNames(), "", true, 140)) s.dirty = true;
    }

    void DrawMaterial(ojson& m)
    {
        std::vector<std::string> err, warn;
        ValidateMat(m, err, warn);
        DrawProblems(err);
        for (auto& w : warn) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "? %s", w.c_str());

        DrawCommon(m);
        if (InputTextKey("tag", m, "tag", 120)) s.dirty = true;
        ImGui::SameLine(); ImGui::TextDisabled(u8"合成カードの名前の【】に入る");

        ImGui::SeparatorText(u8"土台の種類ごとの効果（無い種類は all を使う）");
        for (const char* key : EnumNames::CraftEntryKeys)
        {
            ImGui::PushID(key);
            bool has = m.contains(key);
            if (ImGui::Checkbox(key, &has))
            {
                if (has) m[key] = ojson{ { "slot", "amplifyMain" }, { "value", 1 } };
                else m.erase(key);
                s.dirty = true;
            }
            if (has)
            {
                ojson& e = m[key];
                ImGui::Indent();
                if (ComboKey("slot", e, "slot", SlotNames(), "none", false, 130)) s.dirty = true;
                const SlotInfo* info = FindSlot(e.value("slot", std::string("none")));
                if (info)
                {
                    ImGui::SameLine(); ImGui::TextDisabled("%s", info->hint);
                    if (info->usesType)
                    {
                        if (ComboKey("type", e, "type", EffectNames(), "", false, 130)) s.dirty = true;
                        ImGui::SameLine();
                    }
                    if (info->usesValue)
                    {
                        if (InputIntKey("value", e, "value", 0, 80)) s.dirty = true;
                        ImGui::SameLine();
                    }
                    if (info->usesDuration)
                    {
                        if (InputIntKey("duration", e, "duration", 0, 80)) s.dirty = true;
                        ImGui::SameLine();
                    }
                    ImGui::NewLine();
                    std::string type = e.value("type", std::string());
                    if (type == "ApplyBuff" || type == "ApplyDebuff" || e.contains("buffType"))
                        if (ComboKey("buffType", e, "buffType", BuffNames(), "", false, 130)) s.dirty = true;
                }
                if (ComboKey("vfx", e, "vfx", VfxNames(), "", true, 140)) s.dirty = true;
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void CraftEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool CraftEditor::IsOpen() { return s.open; }

void CraftEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1250, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Craft Editor", &s.open))
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

    // ---- 左：一覧（素材／土台のタブ）----
    ImGui::BeginChild("Left", ImVec2(300, 0), ImGuiChildFlags_ResizeX);
    if (ImGui::BeginTabBar("CraftTabs"))
    {
        if (ImGui::BeginTabItem(u8"素材")) { s.tab = 0; DrawList(Mats(), s.selMat, false); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem(u8"土台")) { s.tab = 1; DrawList(Bases(), s.selBase, true); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 中央：選んだものの中身 ----
    float rightW = 360.0f;
    ImGui::BeginChild("Center", ImVec2(ImGui::GetContentRegionAvail().x - rightW - 8, 0), ImGuiChildFlags_Borders);
    if (s.tab == 0 && s.selMat >= 0 && s.selMat < (int)Mats().size()) DrawMaterial(Mats()[s.selMat]);
    else if (s.tab == 1 && s.selBase >= 0 && s.selBase < (int)Bases().size()) DrawBase(Bases()[s.selBase]);
    else ImGui::TextDisabled("Select a material / base on the left");
    ImGui::EndChild();
    ImGui::SameLine();

    // ---- 右：合成プレビュー ----
    ImGui::BeginChild("Right", ImVec2(0, 0));
    DrawPreviewPanel();
    ImGui::EndChild();

    ImGui::End();
}

void CraftEditor::DrawGameOverlay(SpriteRenderer* sprite, TextRenderer* text)
{
    if (!s.open || !s.overlay || !s.loaded || !s.pvOk) return;
    EditorCardOverlay::Item item = { &s.pvCard, L"合成結果", false };
    EditorCardOverlay::Draw(sprite, text, &item, 1, s.overlayLeft);
}

#else   // Release ではすべて何もしない

void CraftEditor::Toggle() {}
bool CraftEditor::IsOpen() { return false; }
void CraftEditor::Draw() {}
void CraftEditor::DrawGameOverlay(SpriteRenderer*, TextRenderer*) {}

#endif
