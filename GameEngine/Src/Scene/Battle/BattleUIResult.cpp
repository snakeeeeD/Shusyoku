#include "BattleUI.h"
#include "BuffInfo.h"
#include "BuffPopup.h"
#include "Faith.h"
#include "CardExecutor.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "GlossaryLink.h"
#include "EnemyIntentVisual.h"
#include "TerrainDataBase.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "TurnBanner.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "RelicManager.h"
#include "UiWindow.h"
#include "Audio.h"
#include "AnimTune.h"
#include "URect.h"
#include "PileFx.h"
#include <algorithm>
#include <cmath>

using namespace DirectX;

// 勝ち負けの画面（勝利画面・負けのバナー・ドロップの説明）
// （BattleUI のメンバー。BattleUI.cpp から機能ごとに分けたファイル）

// 負けた時の「GAME OVER」と「クリックでタイトルへ」。UI より上の演出（まるのみの顎）の、さらに上に出すため、Draw とは別に呼ぶ
void BattleUI::DrawLoseBanner()
{
    m_textRenderer->Begin();   // 文字は Begin〜End の間でしか描けない（Draw の外から呼ぶので、自分で囲む）
    m_spriteRenderer->Begin();
    const float cx = m_screenWidth / 2.0f;
    m_spriteRenderer->End(); m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"),
        cx - 190.0f, m_screenHeight / 2.0f - 40.0f, 380.0f, 78.0f, 0.0f, XMFLOAT4(0.75f, 0.5f, 0.5f, 1));
    m_textRenderer->DrawOutlinedTextInRect(L"GAME OVER",
        cx - 190.0f, m_screenHeight / 2.0f - 40.0f, 380.0f, 78.0f, 44.0f,   // 布の見出しと同じ四角
        D2D1::ColorF(1.0f, 0.42f, 0.42f), D2D1::ColorF(0.2f, 0.0f, 0.0f), 3.0f);
    const wchar_t* t = L"クリックでタイトルへ";
    const float ph = 36.0f, pw = m_textRenderer->Measure(t, 20.0f, FontRole::Heading).width + 48.0f;
    const float px = cx - pw / 2.0f, py = m_screenHeight - 74.0f;
    m_spriteRenderer->End(); m_spriteRenderer->Begin();
    DrawWindow(px, py, pw, ph);
    m_textRenderer->DrawOutlinedTextInRect(t, px, py, pw, ph, 20.0f,
        D2D1::ColorF(1, 1, 1), D2D1::ColorF(0.1f, 0.1f, 0.1f), 2.0f);
    m_spriteRenderer->End();
    m_textRenderer->End();     // 文字はここでまとめて描かれる（板の上に出る）
}

void BattleUI::DrawDropTooltipTop()
{
    if (m_hoverDropId.empty()) return;
    ItemTooltip::Draw(m_spriteRenderer, m_textRenderer, m_whiteTexture,
        m_hoverDropId, m_hoverDropX, m_hoverDropY, m_screenWidth, m_screenHeight, true);
}

// 勝利画面（ドロップした素材・レリック）
void BattleUI::DrawVictory(const BattleUIContext& ctx)
{
        const float cx = m_screenWidth / 2.0f;

        // ===== VICTORY バナー（布バナー＋アウトライン文字）=====
        const float bw = 350.0f, bh = 78.0f;
        const float bx = cx - bw / 2.0f, by = m_screenHeight / 2.0f - 230.0f;
        m_spriteRenderer->End(); m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"), bx, by, bw, bh, 0.0f, XMFLOAT4(1, 1, 1, 1));
        m_textRenderer->DrawOutlinedTextCentered(L"VICTORY!", cx, by, bw, bh, 46.0f,
            D2D1::ColorF(1.0f, 0.92f, 0.45f), D2D1::ColorF(0.35f, 0.16f, 0.0f), 3.0f);

        float pillY = m_screenHeight / 2.0f + 110.0f;   // 「クリックで次へ」の位置（行が下まで伸びたら押し下げる）

        // ===== ドロップ一覧（アイコン＋窓＋アウトライン文字）=====
        m_hoverDropId.clear();
        if (ctx.drops)
        {
            const float rowW = 300.0f, rowH = 42.0f, gap = 10.0f, icon = 34.0f;
            float y = m_screenHeight / 2.0f - 90.0f;

            for (auto& d : *ctx.drops)
            {
                std::wstring nm; bool isCore = false;
                if (auto mm = MaterialDataBase::GetMaterial(d.id)) nm = ToWString(mm->name);
                else if (auto bb = MaterialDataBase::GetBase(d.id)) { nm = ToWString(bb->name); isCore = true; }
                else nm = ToWString(d.id);

                const float rx = cx - rowW / 2.0f;

                // rare: 窓の後ろで光る帯＋回るきらめき
                if (d.rare)
                {
                    float pulse = 0.5f + 0.5f * sinf(ctx.highlightTimer * 4.0f);
                    m_spriteRenderer->DrawSprite(m_whiteTexture, rx - 8.0f, y - 6.0f, rowW + 16.0f, rowH + 12.0f, 0.0f,
                        XMFLOAT4(1.0f, 0.85f, 0.2f, 0.14f + 0.24f * pulse));
                    for (int k = 0; k < 6; k++)
                    {
                        float a = ctx.highlightTimer * 2.0f + k * 1.0472f;
                        float px = cx + cosf(a) * (rowW / 2.0f + 18.0f);
                        float py = y + rowH / 2.0f + sinf(a) * (rowH / 2.0f + 6.0f);
                        float sz = 4.0f + 3.0f * pulse;
                        m_spriteRenderer->DrawSprite(m_whiteTexture, px - sz / 2, py - sz / 2, sz, sz, 0.0f,
                            XMFLOAT4(1.0f, 0.95f, 0.5f, 0.35f + 0.55f * pulse));
                    }
                }
                m_spriteRenderer->End(); m_spriteRenderer->Begin();   // 光をフラッシュ
                DrawWindow(rx, y, rowW, rowH);
                m_spriteRenderer->End(); m_spriteRenderer->Begin();   // 窓をフラッシュ→アイコンを上へ

                const char* tex = isCore ? "mat_core" : "mat_material";
                m_spriteRenderer->DrawSprite(TextureManager::Get(tex),
                    rx + 8.0f, y + (rowH - icon) / 2.0f, icon, icon, 0.0f, XMFLOAT4(1, 1, 1, 1));

                wchar_t buf[96];
                if (d.count > 1) swprintf_s(buf, L"%s  x%d", nm.c_str(), d.count);
                else             swprintf_s(buf, L"%s", nm.c_str());
                D2D1_COLOR_F col = d.rare ? D2D1::ColorF(1.0f, 0.86f, 0.38f)
                    : D2D1::ColorF(0.96f, 0.96f, 0.96f);
                m_textRenderer->DrawOutlinedText(buf, rx + icon + 18.0f, y + 10.0f, 20.0f,
                    col, D2D1::ColorF(0.10f, 0.06f, 0.02f), 2.5f);
                if (d.rare)
                    m_textRenderer->DrawOutlinedText(L"RARE!", rx + rowW - 66.0f, y + 13.0f, 15.0f,
                        D2D1::ColorF(1.0f, 0.95f, 0.5f), D2D1::ColorF(0.35f, 0.16f, 0.0f), 2.0f);

                if (ctx.mousePos.x >= rx && ctx.mousePos.x <= rx + rowW
                    && ctx.mousePos.y >= y && ctx.mousePos.y <= y + rowH)
                {
                    m_hoverDropId = d.id; m_hoverDropX = rx; m_hoverDropY = y;
                }
                y += rowH + gap;
            }

            // レリック
            if (ctx.rewardRelic && !ctx.rewardRelic->empty())
            {
                const RelicDef* rdef = RelicManager::Get(*ctx.rewardRelic);
                std::wstring rn, rd;
                if (rdef) { rn = ToWString(rdef->name); rd = ToWString(rdef->desc); }
                std::wstring wrapped = CardVisual::WrapText(rd, 19);   // アイコンの分だけ狭く
                int lines = rd.empty() ? 0 : 1;
                for (wchar_t c : wrapped) if (c == L'\n') lines++;
                const float rw = 340.0f, rh = 44.0f + lines * 18.0f, rrx = cx - rw / 2.0f;
                m_spriteRenderer->End(); m_spriteRenderer->Begin();
                DrawWindow(rrx, y, rw, rh);
                m_spriteRenderer->End(); m_spriteRenderer->Begin();   // 窓をフラッシュ→アイコンを上へ
                if (rdef)
                    if (auto tex = TextureManager::Get(rdef->icon))
                    {
                        m_spriteRenderer->DrawSprite(tex, rrx + 11.5f, y + 7.5f, 34.0f, 34.0f, 0.0f, XMFLOAT4(0, 0, 0, 0.65f));   // 影
                        m_spriteRenderer->DrawSprite(tex, rrx + 10.0f, y + 6.0f, 34.0f, 34.0f, 0.0f, RelicManager::RarityColor(rdef->rarity));
                    }
                m_textRenderer->DrawOutlinedText((L"レリック獲得: " + rn).c_str(), rrx + 52.0f, y + 10.0f, 19.0f,
                    D2D1::ColorF(0.95f, 0.8f, 1.0f), D2D1::ColorF(0.2f, 0.1f, 0.25f), 2.5f);
                if (!rd.empty())
                    m_textRenderer->DrawText(wrapped.c_str(), rrx + 52.0f, y + 38.0f, 14.0f, D2D1::ColorF(0.86f, 0.86f, 0.92f));
                if (y + rh + 14.0f > pillY) pillY = y + rh + 14.0f;

            }
        }

        // ===== クリックで次へ（窓ピル）=====
        {
            const wchar_t* t = L"クリックで次へ";
            const float ph = 36.0f, pw = m_textRenderer->Measure(t, 20.0f, FontRole::Heading).width + 48.0f;   // 文字の幅＋左右の余白
            const float px = cx - pw / 2.0f, py = pillY;
            DrawWindow(px, py, pw, ph);
            m_textRenderer->DrawOutlinedTextInRect(t, px, py, pw, ph, 20.0f,
                D2D1::ColorF(1, 1, 1), D2D1::ColorF(0.1f, 0.1f, 0.1f), 2.0f);
        }
}
