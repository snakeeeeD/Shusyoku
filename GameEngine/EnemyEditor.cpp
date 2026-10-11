#include "EnemyEditor.h"

#ifdef _DEBUG

#include "EditorCommon.h"
#include "EditorHub.h"
#include "EnumNames.h"
#include "EnemyDataBase.h"
#include "EnemyIntentVisual.h"
#include "RangeShape.h"
#include "TextureManager.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace EditorCommon;

namespace
{
    constexpr float CELL = 52.0f;   // プレビュー盤面の1マス(px)

    const char* ENEMY_PATH  = "Assets/Data/enemies.json";
    const char* BACKUP_PATH = "Assets/Data/enemies.json.bak";

    struct State
    {
        bool open = false;
        bool loaded = false;
        bool dirty = false;

        JsonFile file;                 // enemies.json そのもの

        int sel = -1;                  // 選択中の敵（doc["enemies"] の添字）
        int act = 0;                   // プレビューする行動

        // プレビュー盤面の位置（データの col/row）
        int ec = PLAYER_COL, er = 1;   // 敵
        int pc = PLAYER_COL, pr = PLAYER_ROW;   // プレイヤー
        int drag = 0;                  // 0:なし 1:敵 2:プレイヤー
        bool showMove = true;          // 接近後の範囲も出す

        std::string status;
    };
    State s;

    ojson& Enemies() { return s.file.doc["enemies"]; }

    // プルダウンの選択肢（表から作る）
    const std::vector<std::string>& RangeNames()     { static auto v = EnumNames::Names(EnumNames::RangeTypes);  return v; }
    const std::vector<std::string>& ApproachNames()  { static auto v = EnumNames::Names(EnumNames::Approaches);  return v; }
    const std::vector<std::string>& AnchorNames()    { static auto v = EnumNames::Names(EnumNames::Anchors);     return v; }
    const std::vector<std::string>& KindNames()      { static auto v = EnumNames::Names(EnumNames::EffectKinds); return v; }
    const std::vector<std::string>& ApplyToNames()   { static auto v = EnumNames::Names(EnumNames::ApplyTos);    return v; }
    const std::vector<std::string>& BuffNames()      { static auto v = EnumNames::Names(EnumNames::BuffTypes);   return v; }
    const std::vector<std::string>& ConditionNames() { static auto v = EnumNames::Names(EnumNames::Conditions);  return v; }
    const std::vector<std::string>& DestNames()      { static auto v = EnumNames::Names(EnumNames::AddCardDestinations); return v; }

    void LoadAll()
    {
        ReloadIdLists();
        if (!s.file.Load(ENEMY_PATH) || !s.file.doc.contains("enemies") || !s.file.doc["enemies"].is_array())
        {
            s.loaded = false;
            s.status = s.file.error.empty() ? "enemies.json has no \"enemies\" array" : s.file.error;
            return;
        }
        s.loaded = true;
        s.dirty = false;
        if (s.sel >= (int)Enemies().size()) s.sel = -1;
        s.status = "Loaded " + std::to_string(Enemies().size()) + " enemies";
    }

    // ==================================================================
    // JSON → ゲームの構造体（EnemyDataBase の Parse と同じ既定値）
    // ==================================================================
    TargetSpec ToTarget(const ojson& j)
    {
        TargetSpec t;
        if (!j.is_object()) return t;
        t.rangeType  = EnumNames::Parse(EnumNames::RangeTypes, j.value("rangeType", std::string("Adjacent")), RangeType::None);
        t.range      = j.value("range", 1);
        t.minRange   = j.value("minRange", 0);
        t.unavoidable = j.value("unavoidable", false);
        t.approach   = EnumNames::Parse(EnumNames::Approaches, j.value("approach", std::string("None")), ApproachType::None);
        t.moveRange  = j.value("moveRange", 1);
        t.anchor     = EnumNames::Parse(EnumNames::Anchors, j.value("anchor", std::string("self")), AnchorType::Self);
        t.anchorCol  = j.value("anchorCol", 0);
        t.anchorRow  = j.value("anchorRow", 0);
        return t;
    }

    Effect ToEffect(const ojson& j)
    {
        Effect e;
        e.kind  = EnumNames::Parse(EnumNames::EffectKinds, j.value("kind", std::string("Damage")), EffectKind::Damage);
        e.value = j.value("value", 0);
        e.buff  = j.value("buff", std::string());
        e.duration = j.value("duration", 0);
        e.hits  = j.value("hits", 1);
        return e;
    }

    // ==================================================================
    // 攻撃範囲のプレビュー（Enemy::IsCellInTarget と同じ判定）
    // ==================================================================
    struct Aim { int dx = 0, dy = 0; };

    Aim AimToPlayer(int ec, int er)
    {
        Aim a;
        int dc = s.pc - ec, dr = s.pr - er;
        if (abs(dc) >= abs(dr)) a.dx = (dc > 0) - (dc < 0);
        else                    a.dy = (dr > 0) - (dr < 0);
        return a;
    }

    bool InTarget(int c, int r, const TargetSpec& t, int ec, int er, const std::vector<std::pair<int, int>>& shape)
    {
        Aim aim = AimToPlayer(ec, er);
        if (t.unavoidable) return true;
        if (t.approach == ApproachType::Dash)
        {
            for (int i = 1; i <= t.moveRange; i++)
                if (ec + aim.dx * i == c && er + aim.dy * i == r) return true;
            return false;
        }
        if (t.anchor != AnchorType::Self)
        {
            int ac = (t.anchor == AnchorType::Player) ? s.pc : t.anchorCol;
            int ar = (t.anchor == AnchorType::Player) ? s.pr : t.anchorRow;
            return RangeShape::Contains(ac, ar, c, r, t.rangeType, t.range, t.minRange, aim.dx, aim.dy);
        }
        for (auto& [dc, dr] : shape)   // 多マス敵は占有マスすべてから判定（Enemy::IsInRange）
            if (RangeShape::Contains(ec + dc, er + dr, c, r, t.rangeType, t.range, t.minRange, aim.dx, aim.dy))
                return true;
        return false;
    }

    bool Threatens(int c, int r, const ojson& action, int ec, int er, const std::vector<std::pair<int, int>>& shape)
    {
        if (action.contains("target") && InTarget(c, r, ToTarget(action["target"]), ec, er, shape)) return true;
        if (action.contains("extraTargets"))
            for (auto& t : action["extraTargets"])
                if (InTarget(c, r, ToTarget(t), ec, er, shape)) return true;
        return false;
    }

    // 接近の経路（Enemy::PlannedMovePath の Toward / Align と同じ歩き方。他の敵は無視）
    std::vector<std::pair<int, int>> ApproachPath(const ojson& action, const std::vector<std::pair<int, int>>& shape)
    {
        std::vector<std::pair<int, int>> path;
        if (!action.contains("target")) return path;
        TargetSpec t = ToTarget(action["target"]);
        if (t.unavoidable || t.moveRange <= 0) return path;

        auto free = [&](int c, int r) { return InBounds(c, r) && !(c == s.pc && r == s.pr); };
        int c = s.ec, r = s.er;

        if (t.approach == ApproachType::Toward)
        {
            if (Threatens(s.pc, s.pr, action, s.ec, s.er, shape)) return path;   // 既に当たるなら動かない
            for (int i = 0; i < t.moveRange; i++)
            {
                int dc = s.pc - c, dr = s.pr - r;
                if (abs(dc) + abs(dr) <= 1) break;
                std::pair<int, int> cand[2]; int n = 0;
                if (abs(dc) >= abs(dr)) { cand[n++] = { c + (dc > 0 ? 1 : -1), r }; if (dr != 0) cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; }
                else                    { cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; if (dc != 0) cand[n++] = { c + (dc > 0 ? 1 : -1), r }; }
                bool moved = false;
                for (int k = 0; k < n && !moved; k++)
                    if (free(cand[k].first, cand[k].second)) { c = cand[k].first; r = cand[k].second; path.push_back({ c, r }); moved = true; }
                if (!moved) break;
            }
        }
        else if (t.approach == ApproachType::Align)
        {
            for (int i = 0; i < t.moveRange; i++)
            {
                int dc = s.pc - c, dr = s.pr - r;
                if (dc == 0 || dr == 0) break;                 // 十字線上＝整列済み
                int nc = c, nr = r;
                if (abs(dc) <= abs(dr)) nc += (dc > 0) ? 1 : -1;   // 小さい方の軸を詰める
                else                    nr += (dr > 0) ? 1 : -1;
                if (!free(nc, nr)) break;
                c = nc; r = nr; path.push_back({ c, r });
            }
        }
        return path;
    }

    // ==================================================================
    // 検証（errors は保存を止める / warnings は表示だけ）
    // ==================================================================
    void CheckTarget(const ojson& t, const std::string& where, std::vector<std::string>& err)
    {
        if (!t.is_object()) return;
        std::string rt = t.value("rangeType", std::string("Adjacent"));
        if (!EnumNames::Contains(EnumNames::RangeTypes, rt)) err.push_back(where + ": unknown rangeType " + rt);
        if (t.contains("approach") && !EnumNames::Contains(EnumNames::Approaches, t.value("approach", std::string())))
            err.push_back(where + ": unknown approach " + t.value("approach", std::string()));
        if (t.contains("anchor") && !EnumNames::Contains(EnumNames::Anchors, t.value("anchor", std::string())))
            err.push_back(where + ": unknown anchor " + t.value("anchor", std::string()));
    }

    void Validate(int index, std::vector<std::string>& err, std::vector<std::string>& warn)
    {
        const ojson& en = Enemies()[index];
        std::string id = en.value("id", std::string());
        if (id.empty()) err.push_back("id is empty");
        for (int i = 0; i < (int)Enemies().size(); i++)
            if (i != index && !id.empty() && Enemies()[i].value("id", std::string()) == id)
            { err.push_back("duplicate id: " + id); break; }

        std::string tex = en.value("texture", std::string());
        if (!TextureManager::Get(tex)) warn.push_back("texture not loaded: " + tex + u8" (SceneManager の Load に追加が必要)");

        if (!en.contains("actions") || en["actions"].empty()) { err.push_back("no actions"); return; }

        int uncondChance = 0;
        const auto& fx = EffectIds();
        for (int a = 0; a < (int)en["actions"].size(); a++)
        {
            const ojson& act = en["actions"][a];
            std::string w = "action " + std::to_string(a);

            if (act.contains("target")) CheckTarget(act["target"], w, err);
            if (act.contains("extraTargets"))
                for (auto& t : act["extraTargets"]) CheckTarget(t, w + " extra", err);

            const ojson sel = act.value("select", ojson::object());
            std::string cond = sel.value("condition", std::string());
            if (!cond.empty() && !EnumNames::Contains(EnumNames::Conditions, cond)) err.push_back(w + ": unknown condition " + cond);
            if (cond.empty()) uncondChance += sel.value("chance", 100);

            for (const char* key : { "vfx", "selfVfx", "windupVfx", "areaVfx", "impactVfx" })
            {
                std::string v = act.value(key, std::string());
                if (!v.empty() && std::find(fx.begin(), fx.end(), v) == fx.end())
                    err.push_back(w + ": unknown " + key + " " + v);
            }

            if (!act.contains("effects")) continue;
            for (auto& e : act["effects"])
            {
                std::string kind = e.value("kind", std::string("Damage"));
                EffectKind k;
                if (!EnumNames::TryParse(EnumNames::EffectKinds, kind, k)) { err.push_back(w + ": unknown kind " + kind); continue; }
                if (e.contains("applyTo") && !EnumNames::Contains(EnumNames::ApplyTos, e.value("applyTo", std::string())))
                    err.push_back(w + ": unknown applyTo " + e.value("applyTo", std::string()));

                std::string buff = e.value("buff", std::string());
                std::string sid = e.value("summonId", std::string());
                if ((k == EffectKind::Buff || k == EffectKind::Debuff) && !EnumNames::Contains(EnumNames::BuffTypes, buff))
                    err.push_back(w + ": unknown buff " + buff);
                if (k == EffectKind::Summon && !FindEnemy(sid))
                    err.push_back(w + ": unknown summon enemy " + sid);
                if (k == EffectKind::AddCard)
                {
                    const auto& cards = CardIds();
                    if (std::find(cards.begin(), cards.end(), sid) == cards.end()) err.push_back(w + ": unknown card " + sid);
                    if (!buff.empty() && !EnumNames::Contains(EnumNames::AddCardDestinations, buff))
                        err.push_back(w + ": unknown destination " + buff);
                }
            }
        }
        if (uncondChance <= 0) warn.push_back("no unconditional action with chance > 0");
    }

    bool Save()
    {
        for (int i = 0; i < (int)Enemies().size(); i++)
        {
            std::vector<std::string> err, warn;
            Validate(i, err, warn);
            if (!err.empty())
            {
                s.sel = i;
                s.status = "Save blocked: fix the problems of " + Enemies()[i].value("id", std::string("?"));
                return false;
            }
        }
        if (!s.file.Save(ENEMY_PATH, BACKUP_PATH)) { s.status = s.file.error; return false; }
        EnemyDataBase::Reload();
        ReloadIdLists();
        s.dirty = false;
        s.status = "Saved (backup: enemies.json.bak)";
        return true;
    }

    std::vector<std::pair<int, int>> ShapeOf(const ojson& en)
    {
        std::vector<std::pair<int, int>> shape;
        if (en.contains("gridShape"))
            for (auto& p : en["gridShape"]) shape.push_back({ p[0].get<int>(), p[1].get<int>() });
        if (shape.empty()) shape.push_back({ 0, 0 });
        return shape;
    }

    // ==================================================================
    // 左：敵の一覧
    // ==================================================================
    void DrawList()
    {
        static char filter[32] = "";
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##find", "search", filter, sizeof(filter));

        ImGui::BeginDisabled(s.sel < 0);
        if (ImGui::Button("Duplicate"))
        {
            ojson e = Enemies()[s.sel];
            std::string base = e.value("id", std::string("enemy")) + "_copy";
            std::string id = base;
            for (int n = 2;; n++)
            {
                bool used = false;
                for (auto& o : Enemies()) if (o.value("id", std::string()) == id) { used = true; break; }
                if (!used) break;
                id = base + std::to_string(n);
            }
            e["id"] = id;
            Enemies().push_back(e);
            s.sel = (int)Enemies().size() - 1;
            s.act = 0;
            s.dirty = true;
        }
        ImGui::EndDisabled();

        ImGui::BeginChild("EnemyList", ImVec2(0, 0), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)Enemies().size(); i++)
        {
            const ojson& en = Enemies()[i];
            std::string id = en.value("id", std::string("?"));
            if (filter[0] && id.find(filter) == std::string::npos) continue;

            std::vector<std::string> err, warn;
            Validate(i, err, warn);

            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##pick", s.sel == i, 0, ImVec2(0, 34))) { s.sel = i; s.act = 0; }
            DrawTextureFit(ImGui::GetWindowDrawList(), TextureManager::Get(en.value("texture", std::string())),
                ImVec2(p.x + 2, p.y + 2), ImVec2(p.x + 32, p.y + 32), IM_COL32(255, 255, 255, 255));
            ImU32 col = !err.empty() ? IM_COL32(255, 110, 110, 255) : IM_COL32(230, 230, 230, 255);
            char info[96];
            snprintf(info, sizeof(info), "%s%s  hp%d", err.empty() ? "" : "! ", id.c_str(), en.value("hp", 0));
            ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 40, p.y + 9), col, info);
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    // ==================================================================
    // 中央：敵の基本情報と行動
    // ==================================================================
    void DrawTargetEditor(ojson& t)
    {
        if (ComboKey("rangeType", t, "rangeType", RangeNames(), "Adjacent", false, 130)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("range", t, "range", 1, 80)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("minRange", t, "minRange", 0, 80)) s.dirty = true;

        if (ComboKey("approach", t, "approach", ApproachNames(), "None", true, 100)) s.dirty = true;
        if (t.contains("approach"))
        {
            ImGui::SameLine();
            if (InputIntKey("moveRange", t, "moveRange", 1, 80)) s.dirty = true;
        }
        ImGui::SameLine();
        if (CheckboxKey("unavoidable", t, "unavoidable")) s.dirty = true;

        if (ComboKey("anchor", t, "anchor", AnchorNames(), "self", true, 100)) s.dirty = true;
        if (t.value("anchor", std::string()) == "fixed")
        {
            ImGui::SameLine();
            if (InputIntKey("anchorCol", t, "anchorCol", 0, 80)) s.dirty = true;
            ImGui::SameLine();
            if (InputIntKey("anchorRow", t, "anchorRow", 0, 80)) s.dirty = true;
        }
    }

    // 効果1つ。種類ごとに必要な項目だけ出す
    void DrawEffectEditor(ojson& e)
    {
        if (ComboKey("kind", e, "kind", KindNames(), "Damage", false, 130)) s.dirty = true;
        EffectKind k = EnumNames::Parse(EnumNames::EffectKinds, e.value("kind", std::string("Damage")), EffectKind::Damage);
        ImGui::SameLine();
        if (InputIntKey("value", e, "value", 0, 80)) s.dirty = true;

        switch (k)
        {
        case EffectKind::Damage:
            ImGui::SameLine();
            if (InputIntKey("hits", e, "hits", 1, 80)) s.dirty = true;
            break;
        case EffectKind::Block:
            ImGui::SameLine();
            if (ComboKey("applyTo", e, "applyTo", ApplyToNames(), "Self", true, 90)) s.dirty = true;
            break;
        case EffectKind::Buff:
        case EffectKind::Debuff:
            if (ComboKey("buff", e, "buff", BuffNames(), "", false, 130)) s.dirty = true;
            ImGui::SameLine();
            if (InputIntKey("duration", e, "duration", 0, 80)) s.dirty = true;
            ImGui::SameLine();
            if (ComboKey("applyTo", e, "applyTo", ApplyToNames(), k == EffectKind::Buff ? "Self" : "Player", true, 90)) s.dirty = true;
            break;
        case EffectKind::Summon:
            if (ComboKey("summonId", e, "summonId", EnemyIds(), "", false, 160)) s.dirty = true;
            ImGui::SameLine(); ImGui::TextDisabled(u8"value = 体数");
            break;
        case EffectKind::AddCard:
            if (ComboKey("card", e, "summonId", CardIds(), "", false, 160)) s.dirty = true;
            ImGui::SameLine();
            if (ComboKey("to", e, "buff", DestNames(), "deck", true, 90)) s.dirty = true;
            ImGui::SameLine(); ImGui::TextDisabled(u8"value = 枚数");
            break;
        case EffectKind::Hazard:
            ImGui::SameLine();
            if (InputIntKey("duration", e, "duration", 0, 80)) s.dirty = true;
            ImGui::TextDisabled(u8"範囲のマスに炎の床。value = やけどの強さ / duration = 残るターン");
            break;
        case EffectKind::Heal:
            ImGui::SameLine(); ImGui::TextDisabled(u8"value 0 = 与ダメージ分を吸収");
            break;
        case EffectKind::MoveToward: case EffectKind::MoveAway:
        case EffectKind::PullPlayer: case EffectKind::KnockbackPlayer: case EffectKind::Coil:
            ImGui::SameLine(); ImGui::TextDisabled(u8"value = マス数");
            break;
        }
    }

    // 説明文の {0} {1} を効果の値に置き換えたもの（ゲームの GetActionText と同じ）
    std::string DescriptionPreview(const ojson& act)
    {
        std::string d = act.value("description", std::string());
        if (!act.contains("effects")) return d;
        for (int i = 0; i < (int)act["effects"].size(); i++)
        {
            std::string key = "{" + std::to_string(i) + "}";
            std::string val = std::to_string(act["effects"][i].value("value", 0));
            size_t pos;
            while ((pos = d.find(key)) != std::string::npos) d.replace(pos, key.size(), val);
        }
        return d;
    }

    // 行動の効果アイコン（ゲームの予告と同じテクスチャ）
    void DrawIntentIcons(const ojson& act)
    {
        if (!act.contains("effects")) return;
        for (auto& j : act["effects"])
        {
            Effect e = ToEffect(j);
            if (!EnemyIntentVisual::ShouldShow(e)) continue;
            ID3D11ShaderResourceView* tex = TextureManager::Get(EnemyIntentVisual::GetIconTexture(e));
            ImGui::SameLine();
            if (tex) ImGui::Image(ImTextureRef((ImTextureID)(intptr_t)tex), ImVec2(18, 18));
            if (EnemyIntentVisual::HasValue(e)) { ImGui::SameLine(0, 2); ImGui::Text("%d", e.value); }
        }
    }

    void DrawActions(ojson& en)
    {
        if (!en.contains("actions")) en["actions"] = ojson::array();
        ojson& actions = en["actions"];

        int moveFrom = -1, moveTo = -1, removeAt = -1, dupAt = -1;
        for (int a = 0; a < (int)actions.size(); a++)
        {
            ojson& act = actions[a];
            ImGui::PushID(a);

            bool previewing = (s.act == a);
            if (previewing) ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(120, 70, 40, 255));
            std::string title = "#" + std::to_string(a) + "  " + DescriptionPreview(act);
            const ojson sel = act.value("select", ojson::object());
            if (sel.contains("condition")) title += "   [if " + sel.value("condition", std::string()) + "]";
            bool open = ImGui::CollapsingHeader((title + "###act").c_str(), ImGuiTreeNodeFlags_AllowOverlap);
            if (previewing) ImGui::PopStyleColor();
            if (ImGui::IsItemClicked()) s.act = a;     // 開閉したらその行動をプレビュー
            DrawIntentIcons(act);

            if (open)
            {
                ImGui::Indent();
                // ---- 並べ替え・複製・削除（順番モードの敵は並び順＝行動順）----
                if (ImGui::SmallButton("Up") && a > 0) { moveFrom = a; moveTo = a - 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("Down") && a + 1 < (int)actions.size()) { moveFrom = a; moveTo = a + 1; }
                ImGui::SameLine();
                if (ImGui::SmallButton("Duplicate")) dupAt = a;
                ImGui::SameLine();
                if (ImGui::SmallButton("Delete")) removeAt = a;
                ImGui::SameLine();
                if (ImGui::SmallButton("Preview")) s.act = a;

                if (InputTextKey("description", act, "description", 320)) s.dirty = true;
                ImGui::TextDisabled(u8"{0} {1} ... = effects[0] [1] ... の value");

                // ---- 選ばれ方 ----
                ImGui::SeparatorText("select");
                if (!act.contains("select")) act["select"] = ojson::object();
                ojson& sl = act["select"];
                if (ComboKey("condition", sl, "condition", ConditionNames(), "", true, 120)) s.dirty = true;
                if (sl.contains("condition"))
                {
                    ImGui::SameLine();
                    if (InputIntKey("conditionValue", sl, "conditionValue", 0, 80)) s.dirty = true;
                }
                ImGui::SameLine();
                if (InputIntKey("chance", sl, "chance", 100, 80)) s.dirty = true;

                // ---- 範囲 ----
                ImGui::SeparatorText("target");
                if (!act.contains("target")) act["target"] = ojson::object();
                ImGui::PushID("target");
                DrawTargetEditor(act["target"]);
                ImGui::PopID();

                if (act.contains("extraTargets"))
                {
                    int rm = -1;
                    for (int t = 0; t < (int)act["extraTargets"].size(); t++)
                    {
                        ImGui::PushID(100 + t);
                        ImGui::SeparatorText(("extraTarget " + std::to_string(t)).c_str());
                        DrawTargetEditor(act["extraTargets"][t]);
                        if (ImGui::SmallButton("remove extra")) rm = t;
                        ImGui::PopID();
                    }
                    if (rm >= 0)
                    {
                        act["extraTargets"].erase(act["extraTargets"].begin() + rm);
                        if (act["extraTargets"].empty()) act.erase("extraTargets");
                        s.dirty = true;
                    }
                }
                if (ImGui::SmallButton("+ extra target"))
                {
                    if (!act.contains("extraTargets")) act["extraTargets"] = ojson::array();
                    act["extraTargets"].push_back(ojson{ { "rangeType", "Row" }, { "range", 1 } });
                    s.dirty = true;
                }

                // ---- 効果 ----
                ImGui::SeparatorText("effects");
                if (!act.contains("effects")) act["effects"] = ojson::array();
                int rmE = -1;
                for (int i = 0; i < (int)act["effects"].size(); i++)
                {
                    ImGui::PushID(200 + i);
                    ImGui::Text("[%d]", i);
                    ImGui::SameLine();
                    DrawEffectEditor(act["effects"][i]);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) rmE = i;
                    ImGui::PopID();
                }
                if (rmE >= 0) { act["effects"].erase(act["effects"].begin() + rmE); s.dirty = true; }
                if (ImGui::SmallButton("+ effect"))
                {
                    act["effects"].push_back(ojson{ { "kind", "Damage" }, { "value", 1 } });
                    s.dirty = true;
                }

                // ---- 演出 ----
                ImGui::SeparatorText("vfx");
                if (ComboKey("vfx", act, "vfx", EffectIds(), "", true, 140)) s.dirty = true;
                ImGui::SameLine(); ImGui::TextDisabled(u8"命中した時、プレイヤーの位置（未指定なら smash）");
                if (ComboKey("selfVfx", act, "selfVfx", EffectIds(), "", true, 140)) s.dirty = true;
                ImGui::SameLine(); ImGui::TextDisabled(u8"行動した時、敵自身の位置（バフ・構えなど）");
                if (ComboKey("impactVfx", act, "impactVfx", EffectIds(), "", true, 140)) s.dirty = true;
                ImGui::SameLine(); ImGui::TextDisabled(u8"着弾点（当たり外れに関係なし）");
                if (ComboKey("areaVfx", act, "areaVfx", EffectIds(), "", true, 140)) s.dirty = true;
                ImGui::SameLine(); ImGui::TextDisabled(u8"範囲の各マス（未指定なら area_burst）");
                if (ComboKey("windupVfx", act, "windupVfx", EffectIds(), "", true, 140)) s.dirty = true;
                if (act.contains("windupVfx"))
                {
                    ImGui::SameLine();
                    if (InputFloatKey("windup(sec)", act, "windup", 0.0f, 90)) s.dirty = true;
                    ImGui::SameLine(); ImGui::TextDisabled(u8"着弾までダメージを待つ");
                }

                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        if (moveFrom >= 0)
        {
            std::swap(actions[moveFrom], actions[moveTo]);
            if (s.act == moveFrom) s.act = moveTo;
            s.dirty = true;
        }
        if (dupAt >= 0)
        {
            actions.insert(actions.begin() + dupAt + 1, actions[dupAt]);
            s.act = dupAt + 1;
            s.dirty = true;
        }
        if (removeAt >= 0)
        {
            actions.erase(actions.begin() + removeAt);
            s.act = 0;
            s.dirty = true;
        }
        if (ImGui::Button("+ Action"))
        {
            actions.push_back(ojson{
                { "description", u8"攻撃、{0}ダメージ" },
                { "target", { { "rangeType", "Adjacent" }, { "range", 1 } } },
                { "effects", ojson::array({ ojson{ { "kind", "Damage" }, { "value", 5 } } }) },
                { "select", { { "chance", 50 } } } });
            s.act = (int)actions.size() - 1;
            s.dirty = true;
        }
    }

    void DrawBasics(ojson& en)
    {
        if (InputTextKey("id", en, "id", 160)) s.dirty = true;
        ImGui::SameLine();
        ImGui::TextDisabled(u8"(idを変えると encounters.json の参照が切れる)");
        if (InputTextKey("texture", en, "texture", 160)) s.dirty = true;
        ImGui::SameLine();
        if (InputIntKey("hp", en, "hp", 10, 90)) s.dirty = true;
        if (InputFloatKey("width", en, "width", 1.0f, 90)) s.dirty = true;
        ImGui::SameLine();
        if (InputFloatKey("height", en, "height", 1.0f, 90)) s.dirty = true;
        if (CheckboxKey("isBoss", en, "isBoss")) s.dirty = true;
        ImGui::SameLine();
        if (CheckboxKey("immovable", en, "immovable")) s.dirty = true;
        ImGui::SameLine();
        if (CheckboxKey("sequential", en, "sequential")) s.dirty = true;
        ImGui::SameLine();
        ImGui::TextDisabled(u8"sequential = 無条件の行動を上から順に");
    }

    // ==================================================================
    // 右：プレビュー盤面
    // ==================================================================
    void DrawPreview(ojson& en)
    {
        ImGui::SeparatorText("Preview");
        ImGui::TextDisabled(u8"敵とプレイヤー(P)はドラッグで移動");
        ImGui::Checkbox("show approach", &s.showMove);

        auto shape = ShapeOf(en);
        const ojson* act = nullptr;
        if (en.contains("actions") && s.act >= 0 && s.act < (int)en["actions"].size()) act = &en["actions"][s.act];

        GridCanvas g;
        g.Begin("preview", CELL);
        g.DrawBoard();

        if (act)
        {
            // 接近後の位置と、そこからの範囲（薄く）
            std::vector<std::pair<int, int>> path;
            if (s.showMove) path = ApproachPath(*act, shape);
            if (!path.empty())
            {
                auto [mc, mr] = path.back();
                for (int r = 0; r < GRID_ROWS; r++)
                    for (int c = 0; c < GRID_COLS; c++)
                        if (Threatens(c, r, *act, mc, mr, shape)) g.FillCell(c, r, IM_COL32(230, 120, 60, 70));
                for (int i = 0; i < (int)path.size(); i++)
                {
                    auto [c, r] = path[i];
                    g.OutlineCell(c, r, IM_COL32(255, 170, 60, 255), 2.0f);
                    char n[8]; snprintf(n, sizeof(n), "%d", i + 1);
                    g.dl->AddText(ImVec2(g.Min(c, r).x + 4, g.Min(c, r).y + 2), IM_COL32(255, 200, 120, 255), n);
                }
            }
            // 今の位置からの範囲
            for (int r = 0; r < GRID_ROWS; r++)
                for (int c = 0; c < GRID_COLS; c++)
                    if (Threatens(c, r, *act, s.ec, s.er, shape)) g.FillCell(c, r, IM_COL32(230, 40, 40, 140));

            // 固定アンカーの中心
            if (act->contains("target") && (*act)["target"].value("anchor", std::string()) == "fixed")
            {
                int ac = (*act)["target"].value("anchorCol", 0), ar = (*act)["target"].value("anchorRow", 0);
                g.OutlineCell(ac, ar, IM_COL32(255, 255, 0, 255), 3.0f);
            }
        }

        // 敵（占有マス＋画像）
        std::vector<std::pair<int, int>> cells;
        for (auto& [dc, dr] : shape) cells.push_back({ s.ec + dc, s.er + dr });
        for (auto& [c, r] : cells) g.OutlineCell(c, r, IM_COL32(255, 255, 255, 120), 1.0f);
        ImVec2 b0, b1;
        g.Bounds(cells, b0, b1);
        DrawTextureFit(g.dl, TextureManager::Get(en.value("texture", std::string())), b0, b1,
            IM_COL32(255, 255, 255, 255), en.value("id", std::string()).c_str());

        // プレイヤー
        g.FillCell(s.pc, s.pr, IM_COL32(60, 110, 200, 200));
        g.dl->AddText(ImVec2(g.Min(s.pc, s.pr).x + CELL * 0.4f, g.Min(s.pc, s.pr).y + CELL * 0.3f), IM_COL32(255, 255, 255, 255), "P");

        // ---- ドラッグ ----
        if (g.HoverCell() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            bool onEnemy = false;
            for (auto& [c, r] : cells) if (c == g.hc && r == g.hr) onEnemy = true;
            s.drag = (g.hc == s.pc && g.hr == s.pr) ? 2 : onEnemy ? 1 : 0;
        }
        if (s.drag != 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && g.HoverCell())
        {
            if (s.drag == 1)
            {
                bool ok = true;
                for (auto& [dc, dr] : shape)
                    if (!InBounds(g.hc + dc, g.hr + dr) || (g.hc + dc == s.pc && g.hr + dr == s.pr)) ok = false;
                if (ok) { s.ec = g.hc; s.er = g.hr; }
            }
            else
            {
                bool onEnemy = false;
                for (auto& [dc, dr] : shape) if (s.ec + dc == g.hc && s.er + dr == g.hr) onEnemy = true;
                if (!onEnemy) { s.pc = g.hc; s.pr = g.hr; }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) s.drag = 0;

        if (act)
        {
            bool hit = Threatens(s.pc, s.pr, *act, s.ec, s.er, shape);
            ImGui::TextColored(hit ? ImVec4(1, 0.4f, 0.4f, 1) : ImVec4(0.6f, 0.9f, 0.6f, 1),
                hit ? u8"プレイヤーに当たる" : u8"プレイヤーに当たらない");
            ImGui::TextDisabled(u8"赤=今の位置から / 橙=接近後（他の敵は無視した目安）");
        }
        ImGui::TextDisabled(u8"enemy (%d,%d)  player (%d,%d)   ※画面右端が col 0", s.ec, s.er, s.pc, s.pr);
    }
}

// ======================================================================
// 公開関数
// ======================================================================
void EnemyEditor::Toggle()
{
    s.open = !s.open;
    if (s.open && !s.loaded) LoadAll();
}

bool EnemyEditor::IsOpen() { return s.open; }

void EnemyEditor::Draw()
{
    if (!s.open) return;

    ImGui::SetNextWindowSize(ImVec2(1320, 780), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Enemy Editor", &s.open))
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
    if (ImGui::Button("Save & Test in Battle") && Save())
    {
        // プレビューの位置に、この敵1体だけで戦闘をやり直してもらう
        EditorHub::RequestBattle({ { Enemies()[s.sel].value("id", std::string()), s.ec, s.er } });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (s.dirty) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* unsaved");
    ImGui::SameLine();
    ImGui::TextDisabled("%s", s.status.c_str());
    ImGui::Separator();

    // ---- 3列 ----
    ImGui::BeginChild("Left", ImVec2(230, 0), ImGuiChildFlags_ResizeX);
    DrawList();
    ImGui::EndChild();
    ImGui::SameLine();

    float previewW = GRID_COLS * CELL + 24;
    ImGui::BeginChild("Center", ImVec2(ImGui::GetContentRegionAvail().x - previewW - 8, 0), ImGuiChildFlags_Borders);
    if (s.sel >= 0 && s.sel < (int)Enemies().size())
    {
        ojson& en = Enemies()[s.sel];
        std::vector<std::string> err, warn;
        Validate(s.sel, err, warn);
        DrawProblems(err);
        for (auto& w : warn) ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "? %s", w.c_str());
        DrawBasics(en);
        ImGui::SeparatorText("actions");
        DrawActions(en);
    }
    else ImGui::TextDisabled("Select an enemy on the left");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("Right", ImVec2(0, 0));
    if (s.sel >= 0 && s.sel < (int)Enemies().size()) DrawPreview(Enemies()[s.sel]);
    ImGui::EndChild();

    ImGui::End();
}

#else   // Release ではすべて何もしない

void EnemyEditor::Toggle() {}
bool EnemyEditor::IsOpen() { return false; }
void EnemyEditor::Draw() {}

#endif
