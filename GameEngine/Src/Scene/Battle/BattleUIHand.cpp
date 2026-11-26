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

// 手札（描画・並び／ホバーのアニメ・入る／抜ける・キーワード・捨てる画面のボタン）

void BattleUI::UpdateCardAnimations(float deltaTime, int handSize, int hoveredIndex,
    int selectedIndex, POINT mousePos, bool selectedNeedsTarget,
    const std::vector<int>* discardSelected)
{
    int prevSize = (int)m_cardAnims.size();

    if (selectedIndex < 0)
        m_cardLockedToCenter = false;

    while ((int)m_cardAnims.size() < handSize)
    {
        CardAnimState anim;
        XMFLOAT2 dp = PileFx::PilePos(PileFx::Pile::Draw, (float)m_screenWidth, (float)m_screenHeight);
        anim.currentX = dp.x;
        anim.currentY = dp.y;
        m_cardAnims.push_back(anim);
    }
    while ((int)m_cardAnims.size() > handSize)
        m_cardAnims.pop_back();

    if (!m_pendingHolds.empty())   // 引いたカード（手札の最後の数枚）を山札で待たせる（リシャッフル演出・効果のドロー）
    {
        int n = (int)m_pendingHolds.size();
        for (int k = 0; k < n; k++)
        {
            int i = handSize - n + k;
            if (i < 0 || i >= handSize) continue;
            if (i < prevSize && m_cardAnims[i].holdTimer <= 0.0f) continue;   // 新しく入った分か、もう待っている分だけ（満杯で捨て札へ行った分は数えない）
            m_cardAnims[i].holdTimer = max(m_cardAnims[i].holdTimer, m_pendingHolds[k]);
            // 自分の空き場所の真下（画面外）から上がってくる。山札（左下）から横に滑らせると、手札の上を横切って入れ替わったように見える
            m_cardAnims[i].currentX = CardVisual::HandSlotX(i, handSize, (float)m_screenWidth);
            m_cardAnims[i].currentY = (float)m_screenHeight + 20.0f;
        }
        m_pendingHolds.clear();
    }

    float cardHideY = m_screenHeight - CardVisual::CARD_HIDE_Y_OFFSET;
    float cardHoverY = m_screenHeight - CardVisual::CARD_H - CardVisual::CARD_HOVER_Y_OFFSET;
    float speed = 12.0f;
    float dt = min(deltaTime, 0.03f);

    for (int i = 0; i < handSize; i++)
    {
        if (m_cardAnims[i].holdTimer > 0.0f) { m_cardAnims[i].holdTimer -= deltaTime; continue; }   // 山札で待機中

        // 呪い(保留)：中央へピークして戻る（自分の手札アニメを一時的に上書き）
        if (m_cardAnims[i].peekTimer > -100.0f)
        {
            m_cardAnims[i].peekTimer += dt;
            if (m_cardAnims[i].peekTimer >= 0.0f)
            {
                const float HOLD = 0.55f;   // 接近＋中央保持の長さ(秒)
                if (m_cardAnims[i].peekTimer < HOLD)
                {
                    float cxT = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
                    float cyT = m_screenHeight / 2.0f - CardVisual::CARD_H / 2.0f + g_animTune.playCenterY;
                    float k = min(1.0f, 14.0f * dt);
                    m_cardAnims[i].currentX += (cxT - m_cardAnims[i].currentX) * k;
                    m_cardAnims[i].currentY += (cyT - m_cardAnims[i].currentY) * k;
                    m_cardAnims[i].currentScale += (1.5f - m_cardAnims[i].currentScale) * k;
                    m_cardAnims[i].currentRot += (0.0f - m_cardAnims[i].currentRot) * k;
                    continue;   // 通常配置を無視して中央へ
                }
                m_cardAnims[i].peekTimer = -1000.0f;   // 終了→通常挙動で手札スロットへ戻る
            }
            // peekTimer < 0（delay待機中）は通常挙動（スロットで待機）
        }

        float targetX = CardVisual::HandSlotX(i, handSize, (float)m_screenWidth);

        // ホバー中のカードの隣を外へ避ける
        if (hoveredIndex >= 0 && i != hoveredIndex)
            targetX += (i < hoveredIndex) ? -18.0f : 18.0f;

        // ホバーで拡大
        float targetScale = (i == hoveredIndex || i == selectedIndex) ? 1.4f : 1.2f;
        m_cardAnims[i].currentScale += (targetScale - m_cardAnims[i].currentScale)
            * min(1.0f, 12.0f * dt);

        float targetY = 0;

        // 扇形に並べる（外側ほど傾く・下がる）
        float center = (handSize - 1) / 2.0f;
        float off = i - center;
        float targetRot = off * 0.03f;                  // ラジアン

        if (i == hoveredIndex || i == selectedIndex)
        {
            targetRot = 0.0f;                            // ホバー中は立てる
        }

        m_cardAnims[i].currentRot += (targetRot - m_cardAnims[i].currentRot)
            * min(1.0f, 12.0f * dt);

        if (i == selectedIndex)
        {
            // 使用ラインより上か（BattleScene の使用判定と同じ線）
            bool abovePlayLine = mousePos.y < m_screenHeight - CardVisual::PLAY_LINE_OFFSET;

            // 上に出したら中央に固定し、選択が外れるまで固定のまま（どの種類のカードも同じ）
            if (abovePlayLine) m_cardLockedToCenter = true;

            if (m_cardLockedToCenter)
            {
                targetX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
                targetY = cardHoverY + 40.0f;
            }
            else
            {
                // 手札のあたりではマウスについていかず、自分の位置で持ち上がったまま待つ（泳がない）
                targetY = cardHoverY;   // targetX は自分のスロットのまま
            }

            if (i < prevSize)
            {
                float dragSpeed = m_cardLockedToCenter ? 8.0f : 15.0f;
                m_cardAnims[i].currentX += (targetX - m_cardAnims[i].currentX) * dragSpeed * dt;
                m_cardAnims[i].currentY += (targetY - m_cardAnims[i].currentY) * dragSpeed * dt;
            }
            continue;
        }
        else if (i == hoveredIndex && selectedIndex >= 0)
            targetY = cardHideY - 40.0f;
        else if (i == hoveredIndex)
            targetY = cardHoverY;
        else
            targetY = cardHideY;

        // 捨てる選択中のカードは上に持ち上げる
        if (discardSelected &&
            std::find(discardSelected->begin(), discardSelected->end(), i)
            != discardSelected->end())
            targetY -= 60.0f;

        // 扇の弧（中央ほど上がる）。ホバー中は平ら  ← 補間より前
        if (i != hoveredIndex)
            targetY += fabsf(off) * fabsf(off) * 2.5f;

        if (i < prevSize)
        {
            m_cardAnims[i].currentX += (targetX - m_cardAnims[i].currentX) * speed * dt;
            m_cardAnims[i].currentY += (targetY - m_cardAnims[i].currentY) * speed * dt;
        }
    }
}

void BattleUI::OnCardRemoved(int index)
{
    if (index >= 0 && index < (int)m_cardAnims.size())
        m_cardAnims.erase(m_cardAnims.begin() + index);
}

// 手札の一番左にカードが入った（敵に入れられたお邪魔カード）。真ん中に少し小さく出して、保留の呪いと同じく中央で見せてから、
// 通常の動きで一番左の場所へ滑らせる。何枚も入った時は delay 秒ずつ隠して待たせ、1枚ずつ見せる
void BattleUI::OnCardInserted(bool front, float delay)
{
    CardAnimState anim;
    anim.currentX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
    anim.currentY = m_screenHeight / 2.0f - CardVisual::CARD_H / 2.0f + g_animTune.playCenterY;
    anim.currentScale = 0.8f;     // 少し小さい所から、中央でふわっと大きく
    anim.holdTimer = delay;       // 待っている間は隠す（押せない）
    anim.peekTimer = 0.0f;        // 隠し終わったら中央で見せてから手札へ
    if (front) m_cardAnims.insert(m_cardAnims.begin(), anim);   // 敵に入れられた状態異常は左端
    else       m_cardAnims.push_back(anim);                     // 効果で加わったカードは右端
}

void BattleUI::StartHandCardPeek(int index, float delay)
{
    if (index >= 0 && index < (int)m_cardAnims.size())
        m_cardAnims[index].peekTimer = -delay;   // delay後に中央へ
}

int BattleUI::GetCardAtScreenPos(POINT p) const
{
    int n = (int)m_cardAnims.size();
    if (n == 0) return -1;

    // 縦は余裕を持たせる（下は画面外まで）
    float topY = m_screenHeight - CardVisual::CARD_HIDE_Y_OFFSET - 40.0f;
    // 上に描かれている（右・ホバー中）カードを優先 → 逆順で最初のヒット
    for (int i = n - 1; i >= 0; i--)
    {
        if (m_cardAnims[i].holdTimer > 0.0f) continue;   // 山札で待機中は押せない
        float x, y, w, h;
        CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, x, y, w, h);   // 実際の描画位置・拡大に一致
        if (p.x >= x && p.x <= x + w && p.y >= topY)
            return i;
    }
    return -1;
}

void BattleUI::DrawCardKeywords(const BattleUIContext& ctx, int idx)
{
    if (idx < 0) return;
    auto& cards = ctx.hand->GetCards();
    if (idx >= (int)cards.size()) return;
    auto kws = CardVisual::GetKeywords(cards[idx]->GetData());
    if (kws.empty()) return;

    const float pw = 320.0f, rowH = 44.0f;
    float ph = 12.0f + kws.size() * rowH;
    float cx, cy, cw, ch; GetCardRect(idx, cx, cy, cw, ch);
    float px = cx + cw / 2.0f - pw / 2.0f;
    float py = cy - ph - 12.0f;                         // カードの上
    if (px < 6.0f) px = 6.0f;
    if (px + pw > m_screenWidth - 6.0f) px = m_screenWidth - 6.0f - pw;
    if (py < 44.0f) py = 44.0f;

    m_textRenderer->End();
    m_spriteRenderer->Begin();
    DrawWindow(px, py, pw, ph);
    m_spriteRenderer->End();
    m_textRenderer->Begin();

    float y = py + 10.0f;
    for (auto& [name, desc] : kws)
    {
        m_textRenderer->DrawText(name.c_str(), px + 12.0f, y, 16.0f, D2D1::ColorF(1.0f, 0.85f, 0.4f));
        m_textRenderer->DrawText(desc.c_str(), px + 12.0f, y + 20.0f, 12.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
        y += rowH;
    }
}

void BattleUI::GetDiscardConfirmRect(float& x, float& y, float& w, float& h) const
{
    w = 140.0f; h = 40.0f;
    x = m_screenWidth / 2.0f - 160.0f;
    y = m_screenHeight - 330.0f;
}

void BattleUI::GetDiscardViewRect(float& x, float& y, float& w, float& h) const
{
    w = 140.0f; h = 40.0f;
    x = m_screenWidth / 2.0f + 20.0f;
    y = m_screenHeight - 330.0f;
}

bool BattleUI::IsOnDiscardConfirm(POINT p) const
{
    float x, y, w, h; GetDiscardConfirmRect(x, y, w, h);
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}

bool BattleUI::IsOnDiscardView(POINT p) const
{
    float x, y, w, h; GetDiscardViewRect(x, y, w, h);
    return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h;
}

// 手札まわり：捨てる選択の暗幕・使ったカードの演出・捨てる画面・手札（上に来るカードは最後に）
void BattleUI::DrawHandArea(const BattleUIContext& ctx)
{
    const auto& cards = ctx.hand->GetCards();
    int topIdx = (ctx.selectedCardIndex >= 0) ? ctx.selectedCardIndex : ctx.hoveredCardIndex;

    // 使用可能カードの発光（縁・パルス）を1枚描く
    auto drawPlayableGlow = [&](int i) {
        if (ctx.discardSelectCount > 0) return;   // 捨て選択中は使用可グローを出さない
        if (i < 0 || i >= (int)m_cardAnims.size()) return;
        if (m_cardAnims[i].holdTimer > 0.0f) return;   // 山札で待機中
        if (ctx.player->CostOf(*cards[i]->GetData(), cards[i]->CostThisTurn()) > ctx.player->GetEnergy()) return;
        float gx, gy, gw, gh;
        CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, gx, gy, gw, gh);
        float pulse = 0.5f + 0.5f * sinf(ctx.highlightTimer * 3.0f);

        // 大きさ違いの3枚（外＝大・淡 → 内＝小・濃）。大きい方から描く
        for (int k = 2; k >= 0; k--)
        {
            float pad = 2.0f + k * 3.0f + 1.5f * pulse;              // 内2 → 外8
            float a = (0.5f + 0.3f * pulse) * (1.0f - k * 0.28f);  // 内濃 → 外淡
            XMFLOAT4 glow(1.0f, 0.8f, 0.2f, a);
            m_spriteRenderer->DrawSprite(m_whiteTexture, gx - pad, gy - pad,
                gw + pad * 2, gh + pad * 2, m_cardAnims[i].currentRot, glow);
        }
    };

    // 捨て選択中：捨てると効果があるカードをティールで派手に強調
    auto drawDiscardGlow = [&](int i) {
        if (ctx.discardSelectCount <= 0) return;
        if (i < 0 || i >= (int)m_cardAnims.size()) return;
        if (m_cardAnims[i].holdTimer > 0.0f) return;   // 山札で待機中
        const CardData* gd = cards[i]->GetData();
        if (!(ctx.burnSelect ? gd->onBurnEffect.hasEffect : gd->onDiscardEffect.hasEffect)) return;   // 燃やす時は「燃やされた時」の効果があるカード
        float gx, gy, gw, gh;
        CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, gx, gy, gw, gh);
        float pulse = 0.5f + 0.5f * sinf(ctx.highlightTimer * 6.0f);
        for (int k = 4; k >= 0; k--)   // 5層の厚いオーラ
        {
            float pad = 3.0f + k * 4.0f + 4.0f * pulse;             // 内3 → 外〜23＋脈動
            float a = (0.80f + 0.20f * pulse) * (1.0f - k * 0.18f); // 濃いめ
            XMFLOAT4 glow = ctx.burnSelect ? XMFLOAT4(1.0f, 0.55f, 0.15f, a)   // 燃やす時は炎の橙
                                           : XMFLOAT4(0.0f, 0.95f, 0.85f, a);  // 捨てる時はティール（4種と非かぶり）
            m_spriteRenderer->DrawSprite(m_whiteTexture, gx - pad, gy - pad,
                gw + pad * 2, gh + pad * 2, m_cardAnims[i].currentRot, glow);
        }
    };

        // 捨てる選択の暗幕：敵のUIも一緒に暗くする（手札と使ったカードは明るいまま）
        bool discarding = ctx.discardSelectCount > 0;
        m_spriteRenderer->Begin();
        if (discarding && !ctx.discardViewMode)
            m_spriteRenderer->DrawSprite(m_whiteTexture, 0.0f, 0.0f,
                (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.55f));

        // 使ったカードの演出：敵のHPや行動予告より手前、手札より奥に描く
        if (!CardTooltip::s_debugHide) DrawPlayCardEffectsFull(ctx);   // 録画用に隠せる（中央へ飛ぶカード）
        DrawDiscardEffectsFull(ctx);
        DrawExhaustEmbers();

        if (discarding)
        {
            int sel = ctx.discardSelected ? (int)ctx.discardSelected->size() : 0;
            bool ready = (sel == ctx.discardSelectCount);

            // 案内の布バナー
            m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"),
                m_screenWidth / 2.0f - 220.0f, m_screenHeight - 410.0f, 440.0f, 76.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));

            // 確定（選び終わるまで灰色）／盤面を見る：窓の枠のボタン、ホバーで浮く
            float x, y, w, h;
            GetDiscardConfirmRect(x, y, w, h);
            bool hc = ready && UiHover(x, y, w, h, ctx.mousePos, "discardOk", s_hoverKey);
            UiWindow::Button(m_spriteRenderer, m_whiteTexture, x, hc ? y - 3.0f : y, w, h,
                !ready ? XMFLOAT4(0.22f, 0.22f, 0.24f, 1.0f)
                : hc ? XMFLOAT4(0.85f, 0.4f, 0.25f, 1.0f) : XMFLOAT4(0.7f, 0.3f, 0.2f, 1.0f));

            GetDiscardViewRect(x, y, w, h);
            bool hv = UiHover(x, y, w, h, ctx.mousePos, "discardView", s_hoverKey);
            UiWindow::Button(m_spriteRenderer, m_whiteTexture, x, hv ? y - 3.0f : y, w, h,
                ctx.discardViewMode ? XMFLOAT4(0.3f, 0.5f, 0.7f, 1.0f)
                : hv ? XMFLOAT4(0.4f, 0.4f, 0.5f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));
        }
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        if (discarding)
        {
            int sel = ctx.discardSelected ? (int)ctx.discardSelected->size() : 0;
            bool ready = (sel == ctx.discardSelectCount);
            wchar_t msg[64];
            swprintf_s(msg, ctx.burnSelect ? L"廃棄するカードを選ぶ（%d/%d）" : L"捨てるカードを選ぶ（%d/%d）", sel, ctx.discardSelectCount);
            m_textRenderer->DrawOutlinedTextCentered(msg, m_screenWidth / 2.0f, m_screenHeight - 410.0f, 440.0f, 76.0f,
                22.0f, D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);

            float x, y, w, h;
            GetDiscardConfirmRect(x, y, w, h);
            float dy = (ready && IsOnDiscardConfirm(ctx.mousePos)) ? -3.0f : 0.0f;
            m_textRenderer->DrawTextCentered(L"確定", x + w / 2.0f, y + dy, w, 20.0f,
                ready ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.5f, 0.5f, 0.5f), h);
            GetDiscardViewRect(x, y, w, h);
            dy = IsOnDiscardView(ctx.mousePos) ? -3.0f : 0.0f;
            m_textRenderer->DrawTextCentered(ctx.discardViewMode ? L"戻る" : L"盤面を見る", x + w / 2.0f, y + dy, w, 20.0f,
                D2D1::ColorF(1, 1, 1), h);
        }

        // 手札：本体→文字 を1枚ずつ交互に描く
        for (int i = 0; i < (int)cards.size(); i++)
        {
            if (i >= (int)m_cardAnims.size()) continue;
            if (i == topIdx) continue;
            if (m_cardAnims[i].holdTimer > 0.0f) continue;   // 山札で待機中（リシャッフル演出の後に出す）

            XMFLOAT4 col = CardVisual::GetCardColor(cards[i]->GetData()->type, false);   // ← 上書きしない（元の色）

            m_textRenderer->End();
            m_spriteRenderer->Begin();
            drawPlayableGlow(i);
            drawDiscardGlow(i);

            // 自傷で死ぬカード：赤いハローを速く点滅（色は変えず危険を示す）
            if (cards[i]->GetData()->selfDamage > 0
                && cards[i]->GetData()->selfDamage >= ctx.player->GetHp())
            {
                float blink = 0.5f + 0.5f * sinf(ctx.highlightTimer * 18.0f);   // 早い点滅
                float rx, ry, rw, rh;
                CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
                    m_cardAnims[i].currentScale, rx, ry, rw, rh);
                float mg = 8.0f;
                m_spriteRenderer->DrawSprite(m_whiteTexture, rx - mg, ry - mg, rw + mg * 2, rh + mg * 2,
                    m_cardAnims[i].currentRot, XMFLOAT4(1.0f, 0.0f, 0.0f, 0.25f + 0.55f * blink));
            }
            CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
                m_cardAnims[i].currentX, m_cardAnims[i].currentY,
                m_cardAnims[i].currentScale, m_cardAnims[i].currentRot,
                col,                                    // ← 元の色に戻す
                cards[i]->GetData(), ctx.highlightTimer);
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            CardVisual::DrawTexts(m_textRenderer, cards[i]->GetData(), ctx.player,
                m_cardAnims[i].currentX, m_cardAnims[i].currentY,
                m_cardAnims[i].currentScale, m_cardAnims[i].currentRot, 1.0f, cards[i]->CostThisTurn());
        }


        // 上に来るカードは、本体 → 文字 の順で最後に描く（下の文字を覆う）
        if (topIdx >= 0 && topIdx < (int)cards.size() && topIdx < (int)m_cardAnims.size())
        {
            m_textRenderer->End();
            m_spriteRenderer->Begin();
            drawPlayableGlow(topIdx);
            drawDiscardGlow(topIdx);

            if (cards[topIdx]->GetData()->selfDamage > 0
                && cards[topIdx]->GetData()->selfDamage >= ctx.player->GetHp())
            {
                float blink = 0.5f + 0.5f * sinf(ctx.highlightTimer * 18.0f);
                float rx, ry, rw, rh;
                CardVisual::GetRect(m_cardAnims[topIdx].currentX, m_cardAnims[topIdx].currentY,
                    m_cardAnims[topIdx].currentScale, rx, ry, rw, rh);
                float mg = 8.0f;
                m_spriteRenderer->DrawSprite(m_whiteTexture, rx - mg, ry - mg, rw + mg * 2, rh + mg * 2,
                    0.0f, XMFLOAT4(1.0f, 0.0f, 0.0f, 0.25f + 0.55f * blink));
            }

            CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
                m_cardAnims[topIdx].currentX, m_cardAnims[topIdx].currentY,
                m_cardAnims[topIdx].currentScale, m_cardAnims[topIdx].currentRot,
                CardVisual::GetCardColor(cards[topIdx]->GetData()->type, false), cards[topIdx]->GetData(), ctx.highlightTimer);
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            CardVisual::DrawTexts(m_textRenderer, cards[topIdx]->GetData(), ctx.player,
                m_cardAnims[topIdx].currentX, m_cardAnims[topIdx].currentY,
                m_cardAnims[topIdx].currentScale, m_cardAnims[topIdx].currentRot, 1.0f, cards[topIdx]->CostThisTurn());
        }
}
