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

    static constexpr float CARD_SPACING = 75.0f;   // 手札の間隔（幅100なので25重なる）

    // 手札 index 番目の基準X（中央揃え）
    static float HandSlotX(int index, int handSize, float screenWidth)
    {
        return screenWidth / 2.0f - (handSize * CARD_SPACING) / 2.0f + index * CARD_SPACING;
    }

    static XMFLOAT4 GetCardColor(CardType type, bool hovered = false)
    {
        XMFLOAT4 color;
        switch (type)
        {
        case CardType::Attack: color = XMFLOAT4(0.9f, 0.2f, 0.2f, 1.0f); break;
        case CardType::Skill:  color = XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f); break;
        case CardType::Move:   color = XMFLOAT4(0.2f, 0.6f, 0.3f, 1.0f); break;
        case CardType::Power:  color = XMFLOAT4(0.8f, 0.2f, 1.0f, 1.0f); break;
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

        size_t sp = result.find(L"{sub}");
        if (sp != std::wstring::npos)
            result.replace(sp, 5, std::to_wstring(data->subEffect.value));

        size_t op = result.find(L"{onhit}");
        if (op != std::wstring::npos)
            result.replace(op, 7, std::to_wstring(data->onHitEffect.value));

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
        float baseX, float baseY, float scale, float rot, const XMFLOAT4& color,
        const CardData* data = nullptr, float time = 0.0f)
    {
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);

        float fw = 0.0f;
        XMFLOAT4 frameCol;
        bool hasFrame = (data != nullptr);
        if (hasFrame)
        {
            switch (data->rarity)
            {
            case CardRarity::Uncommon: frameCol = XMFLOAT4(0.1f, 0.8f, 0.85f, 1.0f); break;     // 水色

            case CardRarity::Rare:     frameCol = XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f); break;

            default:                   frameCol = XMFLOAT4(0.6f, 0.6f, 0.58f, 1.0f); break; // 白っぽい灰
            }
            frameCol.w = color.w;
            fw = 4.0f * scale;
        }

        if (hasFrame && rot == 0.0f)
        {
            // 回転なし（演出中など）：リング状に描いて中央を重ねない
            sr->DrawSprite(white, x, y, w, fw, 0.0f, frameCol);
            sr->DrawSprite(white, x, y + h - fw, w, fw, 0.0f, frameCol);
            sr->DrawSprite(white, x, y + fw, fw, h - fw * 2, 0.0f, frameCol);
            sr->DrawSprite(white, x + w - fw, y + fw, fw, h - fw * 2, 0.0f, frameCol);
        }
        else if (hasFrame)
        {
            sr->DrawSprite(white, x, y, w, h, rot, frameCol);   // 回転時は背面に全面（不透明なので問題なし）
        }

        // 本体（枠のぶん内側）
        sr->DrawSprite(white, x + fw, y + fw, w - fw * 2, h - fw * 2, rot, color);

        // レアはキラキラ
        if (data && data->rarity == CardRarity::Rare)
        {
            for (int i = 0; i < 5; i++)
            {
                float ph = time * 3.0f + i * 1.3f;
                float a = 0.5f + 0.5f * sinf(ph); 
                float sx = x + w * (0.12f + 0.19f * i);
                float sy = y + h * (0.15f + 0.6f * (0.5f + 0.5f * sinf(ph * 0.8f + i)));
                float ss = 5.0f * scale * a;
                sr->DrawSprite(white, sx, sy, ss, ss, 0.0f, XMFLOAT4(1.0f, 0.95f, 0.6f, a));
            }
        }
    }

    // テキストパスで呼ぶ
    static void DrawTexts(TextRenderer* tr, const CardData* data, const Player* player,
        float baseX, float baseY, float scale, float rot, float alpha = 1.0f)
    {
        if (!data) return;
        float x, y, w, h; GetRect(baseX, baseY, scale, x, y, w, h);
        float px = x + w / 2.0f, py = y + h / 2.0f;
        float s = scale;

        bool upgraded = !data->id.empty() && data->id.back() == '+';
        D2D1_COLOR_F nameCol = upgraded
            ? D2D1::ColorF(0.4f, 1.0f, 0.5f, alpha)     // 強化：緑
            : D2D1::ColorF(1, 1, 1, alpha);
        tr->DrawText(data->name.c_str(), x + 5 * s, y + 10 * s, 16 * s, nameCol, rot, px, py);

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