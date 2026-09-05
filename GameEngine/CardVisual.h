#pragma once
#include "CardData.h"
#include "Player.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "RelicManager.h"
#include "RangeShape.h"
#include "TerrainDataBase.h"
#include "TextureManager.h"
#include "RelicManager.h"
#include "BuffInfo.h"
#include "GameUtils.h" 

#include <DirectXMath.h>
#include <string>
#include <set>

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
        case CardType::Status: color = XMFLOAT4(0.45f, 0.40f, 0.50f, 1.0f); break;
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

    // N文字ごとに改行を差し込む（既存の\nはそのまま尊重）
    static std::wstring WrapText(const std::wstring& s, int n)
    {
        if (n <= 0) return s;
        std::wstring out;
        int lineLen = 0;
        for (wchar_t c : s)
        {
            if (c == L'\n') { out += c; lineLen = 0; continue; }
            if (lineLen >= n) { out += L'\n'; lineLen = 0; }
            out += c;
            lineLen++;
        }
        return out;
    }

    static std::wstring GetEffectText(const CardData* data, const Player* player = nullptr, int wrapChars = 8)
    {
        if (!data) return L"";
        if (data->description.empty()) return L"(no desc)";

        int actualValue = data->mainEffect.value;
        if (player)
        {
            if (data->type == CardType::Attack)
            {
                actualValue = player->GetBuffManager().GetFinalAttack(data->mainEffect.value);
                if (std::find(data->tags.begin(), data->tags.end(), "Knife") != data->tags.end())
                    actualValue += player->GetBuffManager().GetBuffValue(BuffType::KnifePower)
                    + RelicManager::SumValue("knifeBonus");

                int hp = player->GetHp(), mhp = player->GetMaxHp();
                if (hp * 2 <= mhp) actualValue += player->GetBuffManager().GetBuffValue(BuffType::LastStand);
                if (hp * 4 <= mhp) actualValue += player->GetBuffManager().GetBuffValue(BuffType::DeepStand);
            }
            else if (data->mainEffect.type == CardEffectType::Block)
                actualValue = player->GetBuffManager().GetFinalBlock(data->mainEffect.value);
            else if (data->mainEffect.type == CardEffectType::PlaceTrap
                || data->mainEffect.type == CardEffectType::PlaceTrapArea)
            {
                const TerrainDef* def = TerrainDataBase::Get(data->mainEffect.trapType);
                if (def)
                {
                    if (def->effect == "Damage")
                        actualValue += RelicManager::SumValue("trapDamage");
                    else if (def->effect == "ApplyDebuff")
                        actualValue += RelicManager::SumValue("trapDebuff");
                }
            }
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
        {
            int ov = data->onHitEffect.value;
            if (data->onHitEffect.buffType == "Poison")
                ov += RelicManager::SumValue("poisonAdd");   // 毒の心得ぶんも表示
            result.replace(op, 7, std::to_wstring(ov));
        }

        size_t hp = result.find(L"{hits}");
        if (hp != std::wstring::npos)
        {
            int hits = data->hits;
            if (data->type == CardType::Attack)
                hits += RelicManager::SumValue("multiHit");   // combo_blade等
            if (hits < 1) hits = 1;
            result.replace(hp, 6, std::to_wstring(hits));      // "{hits}"=6文字
        }

        size_t dp = result.find(L"{ondiscard}");
        if (dp != std::wstring::npos)
            result.replace(dp, 11, std::to_wstring(data->onDiscardEffect.value));

        if (data->exhaust)
            result += L" \n[廃棄]";

        return WrapText(result, wrapChars);
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

        // 本体（カードテクスチャをタイプ色でtint。未ロード時は従来の単色）
        auto cardTex = TextureManager::Get("ui_card");
        sr->DrawSprite(cardTex ? cardTex : white, x + fw, y + fw, w - fw * 2, h - fw * 2, rot, color);

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

        // コストのオーブ
        if (data)
        {
            float px = x + w / 2.0f, py = y + h / 2.0f;
            float os = 30.0f * scale;
            float ocx = x + 4.0f * scale + os / 2.0f;   // 未回転時のオーブ中心
            float ocy = y + 4.0f * scale + os / 2.0f;
            float cs = cosf(rot), sn = sinf(rot);        // カード中心まわりに回す（回転手札対応）
            float rx = px + (ocx - px) * cs - (ocy - py) * sn;
            float ry = py + (ocx - px) * sn + (ocy - py) * cs;
            auto orb = TextureManager::Get("ui_cost");
            sr->DrawSprite(orb ? orb : white, rx - os / 2.0f, ry - os / 2.0f, os, os, rot,
                orb ? XMFLOAT4(1, 1, 1, color.w) : XMFLOAT4(0.15f, 0.13f, 0.25f, color.w));
        }
        // 攻撃範囲のミニ盤面図
        if (data)
        {
            bool selfOnly = (data->rangeType == RangeType::None);   // 自分だけの効果
            float px = x + w / 2.0f, py = y + h / 2.0f;                       // カード中心
            int R = selfOnly ? 1 : (data->range < 1 ? 1 : (data->range > 4 ? 4 : data->range)); // 表示半径（射程に合わせる/最大4）
            int n = 2 * R + 1;
            float foot = 34.0f * scale;                                       // グリッド全体の幅（固定）
            float step = foot / n;
            float cell = step * 0.85f;                                        // セル（隙間を残す）
            float ux0 = x + (w - foot) / 2.0f;                                // 未回転グリッド左上
            float uy0 = y + 39.0f * scale;   
            float cs = cosf(rot), sn = sinf(rot);
            auto put = [&](float ucx, float ucy, float sz, const XMFLOAT4& col) {
                float rcx = px + (ucx - px) * cs - (ucy - py) * sn;          // カード中心まわりに回転
                float rcy = py + (ucx - px) * sn + (ucy - py) * cs;
                sr->DrawSprite(white, rcx - sz / 2.0f, rcy - sz / 2.0f, sz, sz, rot, col);
                };
            put(ux0 + foot / 2.0f, uy0 + foot / 2.0f, foot + 4.0f * scale,
                XMFLOAT4(0.0f, 0.0f, 0.0f, 0.35f * color.w));                 // 背景パネル
            for (int dr = -R; dr <= R; dr++)
                for (int dc = -R; dc <= R; dc++)
                {
                    float ucx = ux0 + (dc + R + 0.5f) * step;
                    float ucy = uy0 + (dr + R + 0.5f) * step;
                    XMFLOAT4 cc;
                    if (dc == 0 && dr == 0)
                        cc = XMFLOAT4(0.9f, 0.95f, 1.0f, color.w);           // プレイヤー
                    else if (!selfOnly && RangeShape::Contains(0, 0, dc, dr, data->rangeType, R, 0, 0, -1))
                        cc = XMFLOAT4(1.0f, 0.85f, 0.3f, color.w);          // 当たるマス
                    else
                        cc = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.12f * color.w);   // 空マス
                    put(ucx, ucy, cell, cc);
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
        // 名前はオーブの右へ
        tr->DrawText(data->name.c_str(), x + 34 * s, y + 11 * s, 13 * s, nameCol, rot, px, py);

        // コストは左上オーブの上に「数字だけ」
        wchar_t cost[16];
        swprintf_s(cost, L"%d", data->cost);
        float os = 30.0f * s;
        float ocx = x + 4.0f * s + os / 2.0f;
        float ocy = y + 4.0f * s + os / 2.0f;
        float fh = 20.0f * s;
        float digitW = (data->cost >= 10 ? 10.0f : 5.0f) * s;
        tr->DrawText(cost, ocx - digitW, ocy - fh * 0.55f, fh,
            D2D1::ColorF(1, 1, 1, alpha), rot, px, py);

        D2D1_COLOR_F dc = IsCardBoosted(data, player)
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f, alpha)
            : D2D1::ColorF(0.8f, 0.8f, 0.8f, alpha);
        tr->DrawText(GetEffectText(data, player, 10).c_str(),
            x + 12 * s, y + 82 * s, 10 * s, dc, rot, px, py);
    }

    // カードが参照するバフ/デバフのキーワード（名前, 説明）を重複なしで
    static std::vector<std::pair<std::wstring, std::wstring>> GetKeywords(const CardData* d)
    {
        std::vector<std::pair<std::wstring, std::wstring>> out;
        if (!d) return out;
        std::set<std::string> seen;
        auto add = [&](const CardEffectData& e) {
            if (e.buffType.empty() || seen.count(e.buffType)) return;
            seen.insert(e.buffType);
            BuffType bt = StringToBuffType(e.buffType);
            out.push_back({ BuffInfo::Get(bt).name, BuffInfo::GetDescription(bt, e.value) });
            };
        add(d->mainEffect); add(d->onHitEffect); add(d->onHitEffect2);
        add(d->onDiscardEffect); add(d->subEffect); add(d->allEnemyEffect);
        return out;
    }
};