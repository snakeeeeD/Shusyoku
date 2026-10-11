#include "EditorCommon.h"

#ifdef _DEBUG

#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace EditorCommon
{
    // ==================================================================
    // JSON ファイル
    // ==================================================================
    namespace
    {
        bool ReadText(const char* path, std::string& out, bool& bom, bool& crlf)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            bom = out.size() >= 3 && (unsigned char)out[0] == 0xEF
                && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF;
            if (bom) out.erase(0, 3);
            crlf = out.find("\r\n") != std::string::npos;
            return true;
        }

        // 2行目の先頭の空白の数＝インデント幅（"{\n  \"enemies\"..." なら 2）
        int DetectIndent(const std::string& text)
        {
            size_t nl = text.find('\n');
            if (nl == std::string::npos) return 1;
            int n = 0;
            for (size_t i = nl + 1; i < text.size() && text[i] == ' '; i++) n++;
            return n > 0 ? n : 1;
        }

        bool WriteText(const char* path, const std::string& text, bool bom, bool crlf)
        {
            std::string body;
            if (crlf)
            {
                body.reserve(text.size() + text.size() / 16);
                for (char c : text) { if (c == '\n') body += '\r'; body += c; }
            }
            else body = text;

            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f) return false;
            if (bom) f.write("\xEF\xBB\xBF", 3);
            f.write(body.data(), (std::streamsize)body.size());
            return (bool)f;
        }
    }

    bool JsonFile::Load(const char* path)
    {
        std::string text;
        if (!ReadText(path, text, bom, crlf)) { error = std::string("cannot open ") + path; return false; }
        ojson j = ojson::parse(text, nullptr, false);   // 例外を投げない
        if (j.is_discarded()) { error = std::string("parse error: ") + path; return false; }
        indent = DetectIndent(text);
        doc = std::move(j);
        error.clear();
        return true;
    }

    bool JsonFile::Save(const char* path, const char* backupPath)
    {
        std::error_code ec;
        if (backupPath)
            std::filesystem::copy_file(path, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (!WriteText(path, doc.dump(indent), bom, crlf)) { error = std::string("cannot write ") + path; return false; }
        return true;
    }

    // ==================================================================
    // 盤面
    // ==================================================================
    void GridCanvas::Begin(const char* id, float cellSize)
    {
        cell = cellSize;
        origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(GRID_COLS * cell, GRID_ROWS * cell),
            ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        hovered = ImGui::IsItemHovered();
        dl = ImGui::GetWindowDrawList();

        ImVec2 m = ImGui::GetIO().MousePos;
        hc = ScreenCol((int)floorf((m.x - origin.x) / cell));   // 画面の列 → データの col
        hr = (int)floorf((m.y - origin.y) / cell);
    }

    void GridCanvas::DrawBoard() const
    {
        for (int r = 0; r < GRID_ROWS; r++)
            for (int c = 0; c < GRID_COLS; c++)
            {
                ImU32 col = ((c + r) & 1) ? IM_COL32(48, 62, 52, 255) : IM_COL32(56, 72, 60, 255);
                dl->AddRectFilled(Min(c, r), Max(c, r), col);
                dl->AddRect(Min(c, r), Max(c, r), IM_COL32(20, 26, 22, 255));
            }
    }

    void GridCanvas::FillCell(int c, int r, ImU32 col) const
    {
        if (InBounds(c, r)) dl->AddRectFilled(Min(c, r), Max(c, r), col);
    }

    void GridCanvas::OutlineCell(int c, int r, ImU32 col, float thick) const
    {
        if (InBounds(c, r)) dl->AddRect(Min(c, r), Max(c, r), col, 0.0f, 0, thick);
    }

    void GridCanvas::Bounds(const std::vector<std::pair<int, int>>& cells, ImVec2& a, ImVec2& b) const
    {
        int minC = 99, minR = 99, maxC = -99, maxR = -99;
        for (auto& [c, r] : cells)
        {
            minC = (std::min)(minC, c); minR = (std::min)(minR, r);
            maxC = (std::max)(maxC, c); maxR = (std::max)(maxR, r);
        }
        a = Min(maxC, minR);   // 反転しているので画面の左端は maxC
        b = Max(minC, maxR);
    }

    namespace
    {
        ImVec2 TextureSize(ID3D11ShaderResourceView* srv)
        {
            static std::unordered_map<ID3D11ShaderResourceView*, ImVec2> cache;
            auto it = cache.find(srv);
            if (it != cache.end()) return it->second;

            ImVec2 size(1, 1);
            ID3D11Resource* res = nullptr;
            srv->GetResource(&res);
            ID3D11Texture2D* tex = nullptr;
            if (res && SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex)))
            {
                D3D11_TEXTURE2D_DESC desc;
                tex->GetDesc(&desc);
                size = ImVec2((float)desc.Width, (float)desc.Height);
                tex->Release();
            }
            if (res) res->Release();
            cache[srv] = size;
            return size;
        }
    }

    void DrawTextureFit(ImDrawList* dl, ID3D11ShaderResourceView* srv, ImVec2 p0, ImVec2 p1,
        ImU32 tint, const char* fallbackText)
    {
        if (srv)
        {
            ImVec2 ts = TextureSize(srv);
            float bw = p1.x - p0.x, bh = p1.y - p0.y;
            float k = (std::min)(bw / ts.x, bh / ts.y);
            ImVec2 sz(ts.x * k, ts.y * k);
            ImVec2 a(p0.x + (bw - sz.x) * 0.5f, p1.y - sz.y);   // 下揃え（足元をマスに合わせる）
            dl->AddImage(ImTextureRef((ImTextureID)(intptr_t)srv), a, ImVec2(a.x + sz.x, a.y + sz.y),
                ImVec2(0, 0), ImVec2(1, 1), tint);
        }
        else if (fallbackText)
        {
            dl->AddText(ImVec2(p0.x + 3, p0.y + 3), IM_COL32(255, 255, 255, 255), fallbackText);
        }
    }

    // ==================================================================
    // ID 一覧
    // ==================================================================
    namespace
    {
        struct IdLists
        {
            bool loaded = false;
            std::vector<EnemyDef> enemies;
            std::vector<std::string> enemyIds, effectIds, cardIds, terrainIds, relicIds, materialIds, encounterIds;
        };
        IdLists g_ids;

        // file[arrayKey][*][idKey] を集める
        std::vector<std::string> CollectIds(const char* path, const char* arrayKey)
        {
            std::vector<std::string> out;
            JsonFile f;
            if (!f.Load(path) || !f.doc.contains(arrayKey)) return out;
            for (auto& e : f.doc[arrayKey])
            {
                std::string id = e.value("id", std::string());
                if (!id.empty()) out.push_back(id);
            }
            return out;
        }

        void EnsureLoaded()
        {
            if (g_ids.loaded) return;
            g_ids = IdLists();
            g_ids.loaded = true;

            JsonFile f;
            if (f.Load("Assets/Data/enemies.json") && f.doc.contains("enemies"))
                for (auto& e : f.doc["enemies"])
                {
                    EnemyDef d;
                    d.id = e.value("id", std::string());
                    d.texture = e.value("texture", std::string());
                    d.boss = e.value("isBoss", false);
                    if (e.contains("gridShape"))
                        for (auto& p : e["gridShape"]) d.shape.push_back({ p[0].get<int>(), p[1].get<int>() });
                    if (d.shape.empty()) d.shape.push_back({ 0, 0 });   // EnemyDataBase と同じく既定は1x1
                    if (d.id.empty()) continue;
                    g_ids.enemyIds.push_back(d.id);
                    g_ids.enemies.push_back(std::move(d));
                }
            g_ids.effectIds = CollectIds("Assets/Data/effects.json", "effects");
            g_ids.cardIds = CollectIds("Assets/Data/cards.json", "cards");
            g_ids.terrainIds = CollectIds("Assets/Data/terrains.json", "terrains");
            g_ids.relicIds = CollectIds("Assets/Data/relics.json", "relics");
            g_ids.materialIds = CollectIds("Assets/Data/materials.json", "bases");
            for (auto& id : CollectIds("Assets/Data/materials.json", "materials")) g_ids.materialIds.push_back(id);
            g_ids.encounterIds = CollectIds("Assets/Data/encounters.json", "encounters");
        }
    }

    const std::vector<EnemyDef>& EnemyDefs() { EnsureLoaded(); return g_ids.enemies; }
    const std::vector<std::string>& EnemyIds() { EnsureLoaded(); return g_ids.enemyIds; }
    const std::vector<std::string>& EffectIds() { EnsureLoaded(); return g_ids.effectIds; }
    const std::vector<std::string>& CardIds() { EnsureLoaded(); return g_ids.cardIds; }
    const std::vector<std::string>& TerrainIds() { EnsureLoaded(); return g_ids.terrainIds; }
    const std::vector<std::string>& RelicIds() { EnsureLoaded(); return g_ids.relicIds; }
    const std::vector<std::string>& MaterialIds() { EnsureLoaded(); return g_ids.materialIds; }
    const std::vector<std::string>& EncounterIds() { EnsureLoaded(); return g_ids.encounterIds; }
    void ReloadIdLists() { g_ids.loaded = false; }

    const EnemyDef* FindEnemy(const std::string& id)
    {
        for (auto& d : EnemyDefs()) if (d.id == id) return &d;
        return nullptr;
    }

    // ==================================================================
    // JSON の項目を直接編集する部品
    // ==================================================================
    bool InputIntKey(const char* label, ojson& obj, const char* key, int def, float width)
    {
        int v = obj.value(key, def);
        ImGui::SetNextItemWidth(width);
        if (!ImGui::InputInt(label, &v)) return false;
        obj[key] = v;
        return true;
    }

    bool InputFloatKey(const char* label, ojson& obj, const char* key, float def, float width)
    {
        float v = obj.value(key, def);
        ImGui::SetNextItemWidth(width);
        if (!ImGui::InputFloat(label, &v, 0.1f, 0.5f, "%.2f")) return false;
        obj[key] = v;
        return true;
    }

    bool CheckboxKey(const char* label, ojson& obj, const char* key, bool def)
    {
        bool v = obj.value(key, def);
        if (!ImGui::Checkbox(label, &v)) return false;
        obj[key] = v;
        return true;
    }

    bool InputTextKey(const char* label, ojson& obj, const char* key, float width)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", obj.value(key, std::string()).c_str());
        ImGui::SetNextItemWidth(width);
        if (!ImGui::InputText(label, buf, sizeof(buf))) return false;
        obj[key] = std::string(buf);
        return true;
    }

    bool InputTextMultilineKey(const char* label, ojson& obj, const char* key, float width, float height)
    {
        char buf[2048];
        snprintf(buf, sizeof(buf), "%s", obj.value(key, std::string()).c_str());
        if (!ImGui::InputTextMultiline(label, buf, sizeof(buf), ImVec2(width, height))) return false;
        obj[key] = std::string(buf);
        return true;
    }

    bool ComboKey(const char* label, ojson& obj, const char* key, const std::vector<std::string>& options,
        const char* def, bool optional, float width)
    {
        bool has = obj.contains(key);
        std::string cur = !has ? (def ? def : "")
            : obj[key].is_string() ? obj[key].get<std::string>() : obj[key].dump();   // 文字列以外が入っていたらそのまま見せる
        bool known = (!has && optional) || std::find(options.begin(), options.end(), cur) != options.end();
        std::string preview = (!has && optional) ? "(none)" : cur;

        bool changed = false;
        ImGui::SetNextItemWidth(width);
        if (!known) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 110, 110, 255));
        bool open = ImGui::BeginCombo(label, preview.c_str());
        if (!known) ImGui::PopStyleColor();
        if (open)
        {
            if (optional && ImGui::Selectable("(none)", !has))
            {
                obj.erase(key);
                changed = true;
            }
            for (auto& o : options)
                if (ImGui::Selectable(o.c_str(), has && o == cur))
                {
                    obj[key] = o;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        return changed;
    }

    void DrawProblems(const std::vector<std::string>& problems)
    {
        for (auto& p : problems)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "! %s", p.c_str());
    }
}

#endif
