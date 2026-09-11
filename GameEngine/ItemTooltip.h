#pragma once
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "UiWindow.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "CardVisual.h"
#include "CardTooltip.h"   // TypeInfo 流用
#include "BuffInfo.h"
#include "GameUtils.h"     // StringToBuffType / StringToCardEffectType
#include <string>
#include <vector>
#include <set>

class ItemTooltip
{
public:
    static bool Info(const std::string& id, std::wstring& name, std::wstring& desc, std::wstring& tag)
    {
        if (auto b = MaterialDataBase::GetBase(id)) { name = ToWString(b->name); desc = ToWString(b->desc); tag = L"コア";     return true; }
        if (auto m = MaterialDataBase::GetMaterial(id)) { name = ToWString(m->name); desc = ToWString(m->desc); tag = L"素材";     return true; }
        if (auto r = RelicManager::Get(id)) { name = ToWString(r->name); desc = ToWString(r->desc); tag = L"レリック"; return true; }
        return false;
    }

    // 素材/コアが関わるバフ・効果のキーワード説明を集める（重複なし）
    static std::vector<std::pair<std::wstring, std::wstring>> Keywords(const std::string& id)
    {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        std::set<std::wstring> seen;
        auto push = [&](const std::wstring& n, const std::wstring& d) {
            if (n.empty() || seen.count(n)) return; seen.insert(n); out.push_back({ n, d });
            };
        auto fromEntry = [&](const MatEntry& e) {
            if (!e.buffType.empty())
            {
                BuffType bt = StringToBuffType(e.buffType);
                push(BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, e.value));
            }
            else if (!e.type.empty())
            {
                std::wstring n, d;
                if (CardTooltip::TypeInfo(StringToCardEffectType(e.type), n, d)) push(n, d);
            }
            if (e.slot == "hits") push(L"連撃", L"攻撃が複数回ヒットする");
            };

        if (auto m = MaterialDataBase::GetMaterial(id))
            for (auto& kv : m->entries) fromEntry(kv.second);
        if (auto b = MaterialDataBase::GetBase(id))
        {
            std::wstring n, d;
            if (CardTooltip::TypeInfo(StringToCardEffectType(b->mainType), n, d)) push(n, d);
        }
        return out;
    }

    static void Draw(SpriteRenderer* sr, TextRenderer* tr, ID3D11ShaderResourceView* white,
        const std::string& id, float ax, float ay, int sw, int sh, bool above = false)
    {
        std::wstring name, desc, tag;
        if (!Info(id, name, desc, tag)) return;
        std::wstring wrapped = CardVisual::WrapText(desc, 18);
        int lines = 1; for (wchar_t c : wrapped) if (c == L'\n') lines++;

        auto kw = Keywords(id);
        const float pw = 300.0f, rowH = 40.0f;
        float headH = 34.0f + lines * 18.0f;
        float ph = headH + kw.size() * rowH + 6.0f;

        float px = ax, py = ay;
        if (above) py = ay - ph - 12.0f;
        if (px + pw > sw - 6.0f) px = sw - 6.0f - pw;
        if (px < 6.0f) px = 6.0f;
        if (py + ph > sh - 6.0f) py = sh - 6.0f - ph;
        if (py < 6.0f) py = 6.0f;

        sr->Begin();
        UiWindow::Draw(sr, white, px, py, pw, ph);
        sr->End();

        tr->Begin();
        tr->DrawText(name.c_str(), px + 12.0f, py + 8.0f, 17.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
        tr->DrawText(tag.c_str(), px + pw - 76.0f, py + 10.0f, 12.0f, D2D1::ColorF(0.7f, 0.8f, 1.0f));
        tr->DrawText(wrapped.c_str(), px + 12.0f, py + 30.0f, 13.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
        float y = py + headH;
        for (auto& [n, d] : kw)
        {
            tr->DrawText(n.c_str(), px + 12.0f, y, 15.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
            tr->DrawText(d.c_str(), px + 12.0f, y + 18.0f, 12.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
            y += rowH;
        }
        tr->End();
    }
};