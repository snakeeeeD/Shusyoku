#pragma once
#include "CardData.h"
#include "Player.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include <DirectXMath.h>
#include <string>

using namespace DirectX;

class CardVisual
{
public:
    static constexpr float CARD_W = 100.0f;
    static constexpr float CARD_H = 140.0f;

    static XMFLOAT4 GetCardColor(CardType type, bool hovered = false)
    {
        XMFLOAT4 color;
        switch (type)
        {
        case CardType::Attack: color = XMFLOAT4(0.9f, 0.2f, 0.2f, 0.8f); break;
        case CardType::Skill:  color = XMFLOAT4(0.2f, 0.4f, 1.0f, 0.8f); break;
        case CardType::Move:   color = XMFLOAT4(0.2f, 0.9f, 0.3f, 0.8f); break;
        case CardType::Power:  color = XMFLOAT4(0.8f, 0.2f, 1.0f, 0.8f); break;
        default:               color = XMFLOAT4(0.7f, 0.7f, 0.7f, 1.0f); break;
        }
        if (hovered) color = XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);
        return color;
    }

    static bool IsCardBoosted(const CardData* data, const Player* player)
    {
        if (!data || !player) return false;
        if (data->type == CardType::Attack)
            return player->GetBuffManager().HasBuff(BuffType::AttackUp);
        if (data->type == CardType::Skill)
            return player->GetBuffManager().HasBuff(BuffType::DefenseUp);
        return false;
    }

    static std::wstring GetEffectText(const CardData* data, const Player* player)
    {
        if (!data) return L"";
        int actualValue = data->mainEffect.value;
        if (player)
        {
            if (data->type == CardType::Attack)
                actualValue = player->GetBuffManager().GetFinalAttack(data->mainEffect.value);
            else if (data->mainEffect.type == CardEffectType::Block)
                actualValue = player->GetBuffManager().GetFinalBlock(data->mainEffect.value);
        }

        std::wstring result = data->description;
        std::wstring placeholder = L"{value}";
        size_t pos = result.find(placeholder);
        if (pos != std::wstring::npos)
            result.replace(pos, placeholder.size(), std::to_wstring(actualValue));

        if (data->exhaust)
            result += L" \n[廃棄]";

        return result;
    }

    // 中心基準スケールの矩形
    static void GetRect(float baseX, float baseY, float scale,
        float& x, float& y, float& w, float& h)
    {
        w = CARD_W * scale; h = CARD_H * scale;
        x = baseX - (w - CARD_W) / 2.0f;
        y = baseY - (h - CARD_H) / 2.0f;
    }

    // スプライトパスで呼ぶ
    static void DrawBase(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        float baseX, float baseY, float scale, float rot, const XMFLOAT4& color)
    {
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);
        sr->DrawSprite(white, x, y, w, h, rot, color);
    }

    // テキストパスで呼ぶ
    static void DrawTexts(TextRenderer* tr, const CardData* data, const Player* player,
        float baseX, float baseY, float scale, float rot, float alpha = 1.0f)
    {
        if (!data) return;
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);
        float px = x + w / 2.0f, py = y + h / 2.0f;
        float s = scale;

        tr->DrawText(data->name.c_str(), x + 5 * s, y + 10 * s, 16 * s,
            D2D1::ColorF(1, 1, 1, alpha), rot, px, py);

        wchar_t cost[32];
        swprintf_s(cost, L"Cost: %d", data->cost);
        tr->DrawText(cost, x + 5 * s, y + 32 * s, 13 * s,
            D2D1::ColorF(1, 1, 0, alpha), rot, px, py);

        D2D1_COLOR_F dc = IsCardBoosted(data, player)
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f, alpha)
            : D2D1::ColorF(0.8f, 0.8f, 0.8f, alpha);
        tr->DrawText(GetEffectText(data, player).c_str(),
            x + 5 * s, y + 55 * s, 12 * s, dc, rot, px, py);
    }
};