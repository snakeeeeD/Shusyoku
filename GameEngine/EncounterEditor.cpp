#include "EncounterEditor.h"

#ifdef _DEBUG

#include "EditorCommon.h"
#include "EditorHub.h"
#include "EncounterDataBase.h"
#include "TextureManager.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace EditorCommon;

namespace
{
    constexpr float CELL = 64.0f;   // エディタ上の1マスの大きさ(px)

    const char* ENCOUNTER_PATH = "Assets/Data/encounters.json";
    const char* BACKUP_PATH    = "Assets/Data/encounters.json.bak";

    const char* CATEGORIES[] = { "normal", "elite", "event", "boss" };
    constexpr int CATEGORY_COUNT = 4;

    // ---- エディタの状態 ----
    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;           // 未保存の変更あり

        JsonFile file;                // encounters.json そのもの

        int sel = -1;                 // 選択中のエンカウント（doc["encounters"] の添字）
        int layerFilter = 0;          // 0 = すべて
        int catFilter = -1;           // -1 = すべて
        int palette = -1;             // 置く敵（EnemyDefs の添字）
        int placed = -1;              // 選択中の配置済み敵（enemies の添字）
        int drag = -1;                // ドラッグ中の配置済み敵

        std::string status;
    };
    State s;

    ojson& Encounters() { return s.file.doc["encounters"]; }

    void LoadAll()
    {
        ReloadIdLists();
        if (!s.file.Load(ENCOUNTER_PATH) || !s.file.doc.contains("encounters") || !s.file.doc["encounters"].is_array())
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "encounters.json has no \"encounters\" array" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        s.placed = s.drag = -1;
        if (s.sel >= (int)Encounters().size()) s.sel = -1;
        s.status = "Loaded " + std::to_string(Encounters().size()) + " encounters, "
            + std::to_string(EnemyDefs().size()) + " enemies";
    }

    // ==================================================================
    // 補助
    // ==================================================================
    // 配置済みの敵1体が占めるマス
    std::vector<std::pair<int, int>> CellsOf(const ojson& e)
    {
        std::vector<std::pair<int, int>> cells;
        int col = e.value("col", 0), row = e.value("row", 0);
        const EnemyDef* d = FindEnemy(e.value("id", std::string()));
        if (!d) { cells.push_back({ col, row }); return cells; }
        for (auto& [dc, dr] : d->shape) cells.push_back({ col + dc, row + dr });
        return cells;
    }

    std::vector<std::pair<int, int>> ShapeOf(const std::string& id)
    {
        const EnemyDef* d = FindEnemy(id);
        return d ? d->shape : std::vector<std::pair<int, int>>{ { 0, 0 } };
    }

    // id の敵を (col,row) に置けるか。ignore は判定から外す配置済みの敵（移動中の自分）
    bool CanPlace(const ojson& enemies, const std::string& id, int col, int row, int ignore)
    {
        std::set<std::pair<int, int>> used;
        for (int i = 0; i < (int)enemies.size(); i++)
        {
            if (i == ignore) continue;
            for (auto& c : CellsOf(enemies[i])) used.insert(c);
        }
        for (auto& [dc, dr] : ShapeOf(id))
        {
            int c = col + dc, r = row + dr;
            if (!InBounds(c, r)) return false;
            if (c == PLAYER_COL && r == PLAYER_ROW) return false;
            if (used.count({ c, r })) return false;
        }
        return true;
    }

    // 1つのエンカウントの問題点
    std::vector<std::string> Validate(int index)
    {
        std::vector<std::string> out;
        const ojson& enc = Encounters()[index];
        std::string encId = enc.value("id", std::string());

        if (encId.empty()) out.push_back("id is empty");
        for (int i = 0; i < (int)Encounters().size(); i++)
            if (i != index && !encId.empty() && Encounters()[i].value("id", std::string()) == encId)
            { out.push_back("duplicate id: " + encId); break; }

        if (!enc.contains("enemies") || enc["enemies"].empty())
        {
            out.push_back("no enemies");
            return out;
        }

        std::set<std::pair<int, int>> used;
        const ojson& enemies = enc["enemies"];
        for (int i = 0; i < (int)enemies.size(); i++)
        {
            std::string id = enemies[i].value("id", std::string());
            if (!FindEnemy(id)) out.push_back("unknown enemy: " + id);
            for (auto& [c, r] : CellsOf(enemies[i]))
            {
                if (!InBounds(c, r))
                    out.push_back(id + " is out of the grid (" + std::to_string(c) + "," + std::to_string(r) + ")");
                else if (c == PLAYER_COL && r == PLAYER_ROW)
                    out.push_back(id + " is on the player start cell");
                else if (!used.insert({ c, r }).second)
                    out.push_back(id + " overlaps another enemy at (" + std::to_string(c) + "," + std::to_string(r) + ")");
            }
        }
        return out;
    }

    bool Save()
    {
        // 問題があるエンカウントが1つでもあれば保存しない（壊れたデータをゲームに渡さない）
        for (int i = 0; i < (int)Encounters().size(); i++)
            if (!Validate(i).empty())
            {
                s.sel = i;
                s.status = "Save blocked: fix the problems of " + Encounters()[i].value("id", std::string("?"));
                return false;
            }

        if (!s.file.Save(ENCOUNTER_PATH, BACKUP_PATH))
        {
            s.status = s.file.error;
            return false;
        }
        EncounterDataBase::Reload();
        s.dirty = false;
        s.status = "Saved (backup: encounters.json.bak)";
        return true;
    }

    std::string UniqueId(const std::string& base)
    {
        for (int n = 1;; n++)
        {
            std::string id = base + "_" + std::to_string(n);
            bool used = false;
            for (auto& e : Encounters()) if (e.value("id", std::string()) == id) { used = true; break; }
            if (!used) return id;
        }
    }

    bool PassFilter(const ojson& enc)
    {
        if (s.layerFilter != 0 && enc.value("layer", 1) != s.layerFilter) return false;
        if (s.catFilter >= 0 && enc.value("category", std::string("normal")) != CATEGORIES[s.catFilter]) return false;
        return true;
    }

    void DrawEnemyImage(ImDrawList* dl, const std::string& id, ImVec2 p0, ImVec2 p1, ImU32 tint)
    {
        const EnemyDef* d = FindEnemy(id);
        DrawTextureFit(dl, d ? TextureManager::Get(d->texture) : nullptr, p0, p1, tint, id.c_str());
    }

    // ==================================================================
    // 左：エンカウント一覧
    // ==================================================================
    void DrawList()
    {
        const char* layers[] = { "All", "L1", "L2", "L3" };
        ImGui::SetNextItemWidth(70);
        ImGui::Combo("##layer", &s.layerFilter, layers, 4);
        ImGui::SameLine();
        const char* cats[] = { "All", "normal", "elite", "event", "boss" };
        int ci = s.catFilter + 1;
        ImGui::SetNextItemWidth(90);
        if (ImGui::Combo("##cat", &ci, cats, 5)) s.catFilter = ci - 1;

        if (ImGui::Button("New"))
        {
            ojson e;
            e["id"] = UniqueId("enc_new");
            e["layer"] = s.layerFilter != 0 ? s.layerFilter : 1;
            e["tier"] = 1;
            e["category"] = s.catFilter >= 0 ? CATEGORIES[s.catFilter] : "normal";
            e["weight"] = 10;
            e["enemies"] = ojson::array();
            Encounters().push_back(e);
            s.sel = (int)Encounters().size() - 1;
            s.placed = -1;
            s.dirty = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(s.sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson e = Encounters()[s.sel];
            e["id"] = UniqueId(e.value("id", std::string("enc")));
            Encounters().push_back(e);
            s.sel = (int)Encounters().size() - 1;
            s.placed = -1;
            s.dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) ImGui::OpenPopup("ConfirmDelete");
        ImGui::EndDisabled();

        if (ImGui::BeginPopupModal("ConfirmDelete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete %s ?", Encounters()[s.sel].value("id", std::string("?")).c_str());
            if (ImGui::Button("Delete"))
            {
                Encounters().erase(Encounters().begin() + s.sel);
                s.sel = -1;
                s.placed = -1;
                s.dirty = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::BeginChild("EncList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)Encounters().size(); i++)
        {
            const ojson& enc = Encounters()[i];
            if (!PassFilter(enc)) continue;
            bool bad = !Validate(i).empty();
            char label[160];
            snprintf(label, sizeof(label), "%s%s  L%d %s w%d (%d)##%d",
                bad ? "! " : "",
                enc.value("id", std::string("?")).c_str(),
                enc.value("layer", 1),
                enc.value("category", std::string("normal")).c_str(),
                enc.value("weight", 0),
                enc.contains("enemies") ? (int)enc["enemies"].size() : 0, i);
            if (bad) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
            if (ImGui::Selectable(label, s.sel == i)) { s.sel = i; s.placed = -1; }
            if (bad) ImGui::PopStyleColor();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中央：プロパティと盤面
    // ==================================================================
    void DrawProperties(ojson& enc)
    {
        if (InputTextKey("id", enc, "id", 160)) s.dirty = true;
        ImGui::SameLine();
        int layer = enc.value("layer", 1), tier = enc.value("tier", 1);
        ImGui::SetNextItemWidth(80);
        if (ImGui::SliderInt("layer", &layer, 1, 3)) { enc["layer"] = layer; s.dirty = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::SliderInt("tier", &tier, 1, 3)) { enc["tier"] = tier; s.dirty = true; }

        std::vector<std::string> cats(CATEGORIES, CATEGORIES + CATEGORY_COUNT);
        if (ComboKey("category", enc, "category", cats, "normal", false, 100)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("weight", enc, "weight", 0, 100))
        {
            enc["weight"] = (std::max)(0, enc["weight"].get<int>());
            s.dirty = true;
        }
    }

    void DrawGrid(ojson& enc)
    {
        if (!enc.contains("enemies")) enc["enemies"] = ojson::array();
        ojson& enemies = enc["enemies"];

        ImGui::TextDisabled("Left: place / drag   Right: remove   P: player start");

        GridCanvas g;
        g.Begin("grid", CELL);
        g.DrawBoard();
        g.FillCell(PLAYER_COL, PLAYER_ROW, IM_COL32(60, 110, 200, 160));
        g.dl->AddText(ImVec2(g.Min(PLAYER_COL, PLAYER_ROW).x + CELL * 0.4f, g.Min(PLAYER_COL, PLAYER_ROW).y + CELL * 0.35f),
            IM_COL32(255, 255, 255, 255), "P");

        // 配置済みの敵（占有マス → 画像）
        for (int i = 0; i < (int)enemies.size(); i++)
        {
            std::string id = enemies[i].value("id", std::string());
            const EnemyDef* d = FindEnemy(id);
            auto cells = CellsOf(enemies[i]);

            ImU32 fill = !d ? IM_COL32(255, 160, 0, 140)              // 未知の敵
                : d->boss ? IM_COL32(150, 70, 190, 110) : IM_COL32(200, 60, 60, 110);
            for (auto& [c, r] : cells) g.FillCell(c, r, fill);

            ImVec2 b0, b1;
            g.Bounds(cells, b0, b1);
            DrawEnemyImage(g.dl, id, ImVec2(b0.x + 2, b0.y + 2), ImVec2(b1.x - 2, b1.y - 2),
                (i == s.drag) ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 255));
            if (i == s.placed) g.dl->AddRect(b0, b1, IM_COL32(255, 220, 60, 255), 0, 0, 3.0f);
        }

        // どの敵がこのマスにいるか
        auto enemyAt = [&](int c, int r) {
            for (int i = (int)enemies.size() - 1; i >= 0; i--)
                for (auto& cell : CellsOf(enemies[i]))
                    if (cell.first == c && cell.second == r) return i;
            return -1;
        };

        int hc = g.hc, hr = g.hr;

        // ゴースト（置く予定・ドラッグ中の位置）
        if (g.HoverCell())
        {
            std::string ghostId;
            int ignore = -1;
            if (s.drag >= 0) { ghostId = enemies[s.drag].value("id", std::string()); ignore = s.drag; }
            else if (s.palette >= 0 && enemyAt(hc, hr) < 0) ghostId = EnemyDefs()[s.palette].id;

            if (!ghostId.empty())
            {
                bool ok = CanPlace(enemies, ghostId, hc, hr, ignore);
                std::vector<std::pair<int, int>> cells;
                for (auto& [dc, dr] : ShapeOf(ghostId)) cells.push_back({ hc + dc, hr + dr });
                for (auto& [c, r] : cells) g.FillCell(c, r, ok ? IM_COL32(80, 220, 120, 120) : IM_COL32(255, 60, 60, 120));
                ImVec2 b0, b1;
                g.Bounds(cells, b0, b1);
                DrawEnemyImage(g.dl, ghostId, b0, b1, IM_COL32(255, 255, 255, 140));
            }
            g.OutlineCell(hc, hr, IM_COL32(255, 255, 255, 200));
        }

        // ---- 操作 ----
        if (g.HoverCell() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            int hit = enemyAt(hc, hr);
            if (hit >= 0) { s.placed = hit; s.drag = hit; }                 // 掴む
            else if (s.palette >= 0)                                        // 置く
            {
                const std::string& id = EnemyDefs()[s.palette].id;
                if (CanPlace(enemies, id, hc, hr, -1))
                {
                    ojson e;
                    e["id"] = id; e["col"] = hc; e["row"] = hr;
                    enemies.push_back(e);
                    s.placed = (int)enemies.size() - 1;
                    s.dirty = true;
                }
                else s.status = "Cannot place " + id + " there";
            }
            else s.placed = -1;
        }
        if (s.drag >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            if (g.HoverCell())
            {
                ojson& e = enemies[s.drag];
                bool moved = e.value("col", 0) != hc || e.value("row", 0) != hr;
                if (moved && CanPlace(enemies, e.value("id", std::string()), hc, hr, s.drag))
                {
                    e["col"] = hc; e["row"] = hr;
                    s.dirty = true;
                }
                else if (moved) s.status = "Cannot move there";
            }
            s.drag = -1;
        }
        if (g.HoverCell() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            int hit = enemyAt(hc, hr);
            if (hit >= 0)
            {
                enemies.erase(enemies.begin() + hit);
                s.placed = -1;
                s.dirty = true;
            }
        }
    }

    // 配置済みの敵を数値でも編集できる一覧
    void DrawPlacedList(ojson& enc)
    {
        ojson& enemies = enc["enemies"];
        ImGui::SeparatorText("Placed (col: right edge = 0)");
        int removeAt = -1;
        for (int i = 0; i < (int)enemies.size(); i++)
        {
            ImGui::PushID(i);
            ojson& e = enemies[i];
            if (ImGui::Selectable(e.value("id", std::string("?")).c_str(), s.placed == i, 0, ImVec2(130, 0))) s.placed = i;
            ImGui::SameLine();
            if (InputIntKey("col", e, "col", 0, 80)) s.dirty = true;
            ImGui::SameLine();
            if (InputIntKey("row", e, "row", 0, 80)) s.dirty = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) removeAt = i;
            ImGui::PopID();
        }
        if (removeAt >= 0) { enemies.erase(enemies.begin() + removeAt); s.placed = -1; s.dirty = true; }
    }

    // ==================================================================
    // 右：敵パレット
    // ==================================================================
    void DrawPalette()
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search enemy", filter, sizeof(filter));

        ImGui::BeginChild("Palette", ImVec2(0, 0), ImGuiChildFlags_Borders);
        const auto& defs = EnemyDefs();
        for (int i = 0; i < (int)defs.size(); i++)
        {
            const EnemyDef& d = defs[i];
            if (filter[0] && d.id.find(filter) == std::string::npos) continue;
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##pick", s.palette == i, 0, ImVec2(0, 40)))
                s.palette = (s.palette == i) ? -1 : i;   // もう一度押すと解除
            DrawEnemyImage(ImGui::GetWindowDrawList(), d.id, ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + 38, p.y + 38),
                IM_COL32(255, 255, 255, 255));
            char info[96];
            snprintf(info, sizeof(info), "%s%s  %dcell", d.id.c_str(), d.boss ? " [boss]" : "", (int)d.shape.size());
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 46, p.y + 12), IM_COL32(230, 230, 230, 255), info);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void EncounterEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool EncounterEditor::IsOpen() { return s.open; }

void EncounterEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1180, 680), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Encounter Editor", &s.open))
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

    // ---- 上部：保存まわり ----
    if (ImGui::Button("Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Revert")) LoadAll();
    ImGui::SameLine();
    ImGui::BeginDisabled(s.sel < 0);
    if (ImGui::Button("Save & Play in Battle") && Save())
    {
        // 保存した敵の並びで、戦闘をやり直してもらう
        std::vector<EncounterEnemy> list;
        for (auto& e : Encounters()[s.sel]["enemies"])
            list.push_back({ e.value("id", std::string()), e.value("col", 0), e.value("row", 0) });
        EditorHub::RequestBattle(list);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (s.dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* unsaved");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", s.status.c_str());
    ImGui::Separator();

    // ---- 3列 ----
    ImGui::BeginChild("Left", ImVec2(300, 0), ImGuiChildFlags_ResizeX);
    DrawList();
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Center", ImVec2(GRID_COLS * CELL + 24, 0));
    if (s.sel >= 0 && s.sel < (int)Encounters().size())
    {
        ojson& enc = Encounters()[s.sel];
        DrawProperties(enc);
        DrawGrid(enc);
        DrawProblems(Validate(s.sel));
        DrawPlacedList(enc);
    }
    else ImGui::TextDisabled("Select an encounter on the left");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Right", ImVec2(0, 0));
    DrawPalette();
    ImGui::EndChild();

    ImGui::End();
}

#else   // Release ではすべて何もしない

void EncounterEditor::Toggle() {}
bool EncounterEditor::IsOpen() { return false; }
void EncounterEditor::Draw() {}

#endif
