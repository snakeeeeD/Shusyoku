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

// 山札・捨て札・廃棄札の一覧の窓

// 矩形rに収まるように置く時の CardVisual の基準座標と倍率（GetRectの逆）
static void CardBaseOf(const URect& r, float& bx, float& by, float& s)
{
    s = r.w / CardVisual::CARD_W;
    bx = r.x - CardVisual::CARD_W * (1.0f - s) / 2.0f;
    by = r.y - CardVisual::CARD_H * (1.0f - s) / 2.0f;
}

URect BattleUI::PileViewerRect() const
{
    return { (m_screenWidth - 860.0f) / 2.0f, 80.0f, 860.0f, 560.0f };
}

URect BattleUI::PileCardRect(int i, int count) const
{
    bool many = (count > 36);   // 多い時は小さくして全部収める
    float s = many ? 0.6f : 0.75f;
    int cols = many ? 11 : 9;
    float gap = many ? 10.0f : 12.0f;
    URect v = PileViewerRect();
    float w = CardVisual::CARD_W * s, h = CardVisual::CARD_H * s;
    float gridW = cols * w + (cols - 1) * gap;
    return { v.x + (v.w - gridW) / 2.0f + (i % cols) * (w + gap),
             v.y + 64.0f + (i / cols) * (h + gap), w, h };
}

int BattleUI::PileCardAt(POINT p, int count) const
{
    for (int i = 0; i < count; i++)
        if (PileCardRect(i, count).has(p)) return i;
    return -1;
}

std::vector<std::string> BattleUI::PileForView(Deck* deck, bool showDraw, bool showDiscard, int hideIndex)
{
    std::vector<std::string> v = showDraw ? deck->GetDrawPile()
        : showDiscard ? deck->GetDiscardPile() : deck->GetExhaustPile();
    if (hideIndex >= 0 && hideIndex < (int)v.size()) v.erase(v.begin() + hideIndex);   // 出さない1枚（並べ替えの前に消す）
    if (showDraw) std::sort(v.begin(), v.end());   // 山札は引く順が分からないように並べ替える
    return v;
}

void BattleUI::DrawPileViewer(const BattleUIContext& ctx)
{
    auto pile = PileForView(ctx.deck, ctx.showDrawPile, ctx.showDiscardPile, ctx.pileHideIndex);
    int n = (int)pile.size();
    URect v = PileViewerRect();
    int hover = PileCardAt(ctx.mousePos, n);

    const wchar_t* name = ctx.cardSelecting
        ? (ctx.showDrawPile ? L"山札から選ぶ" : ctx.showDiscardPile ? L"捨て札から選ぶ" : L"廃棄札から選ぶ")
        : (ctx.showDrawPile ? L"山札" : ctx.showDiscardPile ? L"捨て札" : L"廃棄");
    wchar_t title[48]; swprintf_s(title, L"%s（%d枚）", name, n);
    const float bnW = 400.0f, bnH = 80.0f, bnX = v.x + v.w / 2.0f - bnW / 2.0f, bnY = v.y - 40.0f;

    // ---- スプライト ----
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, 0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(0, 0, 0, 0.45f));   // 薄い暗幕
    DrawWindow(v.x, v.y, v.w, v.h);
    m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"), bnX, bnY, bnW, bnH, 0.0f, XMFLOAT4(1, 1, 1, 1));
    for (int i = 0; i < n; i++)
    {
        const CardData* d = CardDataBase::Get(pile[i]);
        if (!d) continue;
        URect r = PileCardRect(i, n);
        if (ctx.cardSelecting && i == hover)   // 選べる時は金の縁
            UiWindow::Frame(m_spriteRenderer, m_whiteTexture, r.x, r.y, r.w, r.h, XMFLOAT4(1.0f, 0.85f, 0.2f, 1.0f));
        float bx, by, s; CardBaseOf(r, bx, by, s);
        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, bx, by, s, 0.0f,
            CardVisual::GetCardColor(d->type, false), d, 0.0f);
    }
    m_spriteRenderer->End();

    // ---- 文字 ----
    m_textRenderer->Begin();
    m_textRenderer->DrawOutlinedTextCentered(title, bnX + bnW / 2.0f, bnY, bnW, bnH, 24.0f,
        D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);
    for (int i = 0; i < n; i++)
    {
        const CardData* d = CardDataBase::Get(pile[i]);
        if (!d) continue;
        float bx, by, s; CardBaseOf(PileCardRect(i, n), bx, by, s);
        CardVisual::DrawTexts(m_textRenderer, d, ctx.player, bx, by, s, 0.0f, 1.0f);
    }
    if (n == 0)
        m_textRenderer->DrawTextCentered(L"カードがありません", v.x + v.w / 2.0f, v.y, v.w, 20.0f,
            D2D1::ColorF(0.7f, 0.7f, 0.7f), v.h);
    m_textRenderer->DrawTextCentered(
        ctx.cardSelecting ? L"カードをクリックして選ぶ（枠の外で取りやめ）" : L"枠の外をクリックで閉じる",
        v.x + v.w / 2.0f, v.y + v.h - 34.0f, v.w, 15.0f, D2D1::ColorF(0.75f, 0.75f, 0.75f), 24.0f);
    m_textRenderer->End();

    // ---- ホバー中のカードを拡大＋キーワード説明（最前面） ----
    if (hover >= 0)
        if (const CardData* d = CardDataBase::Get(pile[hover]))
        {
            const float Z = 1.6f;
            URect r = PileCardRect(hover, n);
            float w = CardVisual::CARD_W * Z, h = CardVisual::CARD_H * Z;
            float tx = r.x + r.w / 2.0f - w / 2.0f, ty = r.y + r.h / 2.0f - h / 2.0f;   // 小さいカードの中心に合わせる
            if (tx < 6.0f) tx = 6.0f;
            if (tx + w > m_screenWidth - 6.0f) tx = m_screenWidth - 6.0f - w;
            if (ty < 6.0f) ty = 6.0f;
            if (ty + h > m_screenHeight - 6.0f) ty = m_screenHeight - 6.0f - h;
            float bx, by, s; CardBaseOf({ tx, ty, w, h }, bx, by, s);

            m_spriteRenderer->Begin();
            CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, bx, by, s, 0.0f,
                CardVisual::GetCardColor(d->type, false), d, 0.0f);
            m_spriteRenderer->End();
            m_textRenderer->Begin();
            CardVisual::DrawTexts(m_textRenderer, d, ctx.player, bx, by, s, 0.0f, 1.0f);
            m_textRenderer->End();
            CardTooltip::Draw(m_spriteRenderer, m_textRenderer, m_whiteTexture, d,
                tx + w / 2.0f, ty, w, h, m_screenWidth, m_screenHeight, bnY + bnH);   // 見出しのバナーより上には出さない
        }
}
