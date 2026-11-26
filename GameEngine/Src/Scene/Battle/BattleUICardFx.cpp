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

// カードを使う・捨てる・燃える 演出（燃えカス・溶ける絵も）

// 光の帯の色：カードの種類の色を、少し白に寄せて明るく
static XMFLOAT4 StreakColor(CardType t)
{
    XMFLOAT4 c = CardVisual::GetCardColor(t, false);
    return XMFLOAT4(c.x * 0.6f + 0.4f, c.y * 0.6f + 0.4f, c.z * 0.6f + 0.4f, 0.9f);
}

void BattleUI::StartDiscardEffects()
{
    for (auto& anim : m_cardAnims)
    {
        DiscardCardEffect effect;
        effect.startX = anim.currentX;
        effect.startY = anim.currentY;
        effect.alpha = 1.0f;
        effect.timer = 0.0f;
        effect.done = false;
        m_discardCardEffects.push_back(effect);
    }
}

void BattleUI::StartOverflowDiscardEffect()
{
    DiscardCardEffect effect;
    effect.startX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;   // 手札の位置から
    effect.startY = m_screenHeight - 320.0f;   // 手札より上から（隠れないように）
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    m_discardCardEffects.push_back(effect);
}

void BattleUI::UpdateDiscardEffects(float deltaTime)
{
    for (auto& e : m_discardCardEffects)
    {
        if (e.done) continue;
        if (e.delay > 0.0f) { e.delay -= deltaTime; continue; }   // 遅延中は待機
        if (!e.exhaust && !e.streaked)   // 捨て札へ：カードと同じ道で光の帯
        {
            XMFLOAT2 from = { e.startX + CardVisual::CARD_W * 0.5f, e.startY + CardVisual::CARD_H * 0.5f };
            PileFx::AddStreak(from, PileFx::PileCenter(PileFx::Pile::Discard, (float)m_screenWidth, (float)m_screenHeight),
                DISCARD_ARC, EXHAUST_FADE_DUR, 0.0f, StreakColor(e.cardType), PileFx::Pile::Discard, false, 4.0f);
            e.streaked = true;
        }
        if (e.exhaust && !e.sparked)
        {
            SpawnExhaustEmbers(e.startX + CardVisual::CARD_W * 0.5f, e.startY + CardVisual::CARD_H * 0.5f,
                CardVisual::CARD_W, CardVisual::CARD_H);
            e.sparked = true;
        }
        e.timer += deltaTime;
        float dur = e.exhaust ? EXHAUST_BURN_DUR : DISCARD_EFFECT_DUR;
        float t = min(1.0f, e.timer / dur);
        e.alpha = 1.0f - t;
        if (t >= 1.0f) e.done = true;
    }
    m_discardCardEffects.erase(
        std::remove_if(m_discardCardEffects.begin(), m_discardCardEffects.end(),
            [](const DiscardCardEffect& e) { return e.done; }),
        m_discardCardEffects.end());
}

void BattleUI::DrawCardBurning(const BattleUIContext& ctx, const CardData* data, CardType type,
    float baseX, float baseY, float scale, float rot, float progress)
{
    // 1. カード（枠・絵・文字）を、別の絵に描いて1枚にまとめる
    m_spriteRenderer->End();
    if (!m_cardBurn.BeginCapture(m_textRenderer)) { m_spriteRenderer->Begin(); return; }
    m_spriteRenderer->Begin();
    CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, baseX, baseY, scale, rot, CardVisual::GetCardColor(type, false), data, 0.0f);
    m_spriteRenderer->End();
    m_textRenderer->Begin();
    CardVisual::DrawTexts(m_textRenderer, data, ctx.player, baseX, baseY, scale, rot, 1.0f);
    m_textRenderer->End();
    m_cardBurn.EndCapture(m_textRenderer);

    // 2. その1枚の、カードを囲む四角の範囲を、溶けるシェーダーで画面に描く（傾いている分だけ四角を広げる）
    float rx, ry, rw, rh; CardVisual::GetRect(baseX, baseY, scale, rx, ry, rw, rh);
    float c = fabsf(cosf(rot)), s = fabsf(sinf(rot));
    float bw = rw * c + rh * s + 8.0f, bh = rw * s + rh * c + 8.0f;
    float cx = rx + rw * 0.5f, cy = ry + rh * 0.5f;
    float seed = baseX * 0.013f + baseY * 0.007f;   // カードごとに模様を変える（毎フレーム同じ値になるよう、位置から決める）
    m_cardBurn.Draw(ctx.renderer3D, cx - bw * 0.5f, cy - bh * 0.5f, bw, bh, progress, seed);
    m_spriteRenderer->Begin();
}

void BattleUI::DrawDiscardEffectsFull(const BattleUIContext& ctx)
{
    // 呼び出し時はスプライトパス中
    XMFLOAT2 dc = PileFx::PileCenter(PileFx::Pile::Discard, (float)m_screenWidth, (float)m_screenHeight);
    float targetX = dc.x - CardVisual::CARD_W * 0.5f, targetY = dc.y - CardVisual::CARD_H * 0.5f;   // 捨て札の真ん中にカードの真ん中が着く
    for (auto& e : m_discardCardEffects)
    {
        float s = e.startScale, rot = e.startRot, x, y;
        XMFLOAT4 c = CardVisual::GetCardColor(e.cardType, false);
        if (e.delay > 0.0f) { x = e.startX; y = e.startY; }
        else if (e.exhaust)   // 廃棄：その場で文字ごと焼けて溶ける
        {
            DrawCardBurning(ctx, e.data, e.cardType, e.startX, e.startY, e.startScale, e.startRot,
                min(1.0f, e.timer / EXHAUST_BURN_DUR));
            continue;
        }
        else {                                // 通常：捨て札へスライド
            float t = min(1.0f, e.timer / EXHAUST_FADE_DUR);
            XMFLOAT2 p = PileFx::PathAt({ e.startX, e.startY }, { targetX, targetY }, DISCARD_ARC, t);   // 光の帯と同じ道
            x = p.x; y = p.y;
            s = e.startScale * (1.0f - 0.6f * t);   // 小さくなりながら
        }
        c.w = e.alpha;
        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, x, y, s, rot, c, e.data, 0.0f);
        m_spriteRenderer->End(); m_textRenderer->Begin();
        CardVisual::DrawTexts(m_textRenderer, e.data, ctx.player, x, y, s, rot, e.alpha);
        m_textRenderer->End(); m_spriteRenderer->Begin();
    }
}

void BattleUI::UpdatePlayCardEffects(float deltaTime)
{
    for (auto& e : m_playCardEffects)
    {
        e.timer += deltaTime;
        float et = e.timer - e.delay;                  // ← 遅延を反映
        if (et < 0.0f) { e.alpha = 0.0f; continue; }   // 出番前は非表示
        if (e.isBurn)
        {
            float t = min(1.0f, et / BURN_EFFECT_DUR);
            e.alpha = (t < 0.58f) ? 1.0f : 1.0f - (t - 0.58f) / 0.42f;
            if (t >= 1.0f) e.done = true;
        }
        else
        {
            // 捨て札へ：飛び始め（GetPlayEffectTransform と同じ時刻）に、カードと同じ道で光の帯
            if (!e.exhaust && !e.streaked && et >= g_animTune.playHold * PLAY_EFFECT_DUR)
            {
                float cx = m_screenWidth / 2.0f, cy = m_screenHeight / 2.0f + g_animTune.playCenterY;   // カードの真ん中
                PileFx::AddStreak({ cx, cy }, PileFx::PileCenter(PileFx::Pile::Discard, (float)m_screenWidth, (float)m_screenHeight),
                    g_animTune.playArc, DISCARD_EFFECT_DUR, 0.0f, StreakColor(e.cardType), PileFx::Pile::Discard);
                e.streaked = true;
            }
            float holdEnd = 0.72f * PLAY_EFFECT_DUR;
            if (et < holdEnd)
                e.alpha = 1.0f;
            else
            {
                e.alpha = 1.0f - min(1.0f, (et - holdEnd) / EXHAUST_FADE_DUR);
                if (e.exhaust && !e.sparked)     // 廃棄：燃え始めに火の粉
                {
                    SpawnExhaustEmbers(m_screenWidth / 2.0f,
                        m_screenHeight / 2.0f + g_animTune.playCenterY, CardVisual::CARD_W, CardVisual::CARD_H);
                    e.sparked = true;
                }
                if (e.exhaust && et >= holdEnd + EXHAUST_BURN_DUR) e.done = true;
                else if (!e.exhaust && et >= holdEnd + EXHAUST_FADE_DUR) e.done = true;
            }
            if (et >= holdEnd + DISCARD_EFFECT_DUR) e.done = true;
        }
    }
    m_playCardEffects.erase(
        std::remove_if(m_playCardEffects.begin(), m_playCardEffects.end(),
            [](const PlayCardEffect& e) { return e.done; }),
        m_playCardEffects.end());

    UpdateExhaustEmbers(deltaTime);
}

void BattleUI::StartPlayCardEffect(CardType type, float fromX, float fromY)
{
    PlayCardEffect effect;
    effect.startX = fromX;
    effect.startY = fromY;
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    effect.cardType = type;
    m_playCardEffects.push_back(effect);
}

void BattleUI::StartPlayCardEffect(CardType type, int cardIndex)
{
    PlayCardEffect effect;
    if (cardIndex >= 0 && cardIndex < (int)m_cardAnims.size())
    {
        effect.startX = m_cardAnims[cardIndex].currentX;   // 今カードがある位置
        effect.startY = m_cardAnims[cardIndex].currentY;
    }
    else
    {
        effect.startX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
        effect.startY = (float)m_screenHeight - CardVisual::CARD_H;
    }
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    effect.cardType = type;
    m_playCardEffects.push_back(effect);
}

void BattleUI::StartPlayCardEffect(const CardData* data, int cardIndex)
{
    PlayCardEffect effect;
    if (cardIndex >= 0 && cardIndex < (int)m_cardAnims.size())
    {
        effect.startX = m_cardAnims[cardIndex].currentX;
        effect.startY = m_cardAnims[cardIndex].currentY;
    }
    else { effect.startX = m_screenWidth / 2.0f; effect.startY = (float)m_screenHeight - CardVisual::CARD_H; }
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    effect.data = data;
    effect.exhaust = data && (data->exhaust || data->ethereal);
    effect.cardType = data ? data->type : CardType::Skill;
    m_playCardEffects.push_back(effect);
}

void BattleUI::StartPlayCardEffect(const CardData* data, float startX, float startY, float delay, bool isBurn)
{
    PlayCardEffect e;
    e.startX = startX; e.startY = startY;
    e.alpha = 1.0f; e.timer = 0.0f; e.done = false;
    e.cardType = data ? data->type : CardType::Status;
    e.data = data; e.delay = delay;
    e.exhaust = data && (data->exhaust || data->ethereal);
    e.isBurn = isBurn;
    m_playCardEffects.push_back(e);
}

void BattleUI::StartPlayCardEffectFromHand(const CardData* data, int cardIndex, float delay, bool isBurn)
{
    PlayCardEffect e;
    if (cardIndex >= 0 && cardIndex < (int)m_cardAnims.size()) {
        e.startX = m_cardAnims[cardIndex].currentX;
        e.startY = m_cardAnims[cardIndex].currentY;
        e.startScale = m_cardAnims[cardIndex].currentScale;
        e.startRot = m_cardAnims[cardIndex].currentRot;
    }
    else {
        e.startX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
        e.startY = (float)m_screenHeight - CardVisual::CARD_H;
    }
    e.alpha = 1.0f; e.timer = 0.0f; e.done = false;
    e.cardType = data ? data->type : CardType::Status;
    e.data = data; e.delay = delay; e.isBurn = isBurn;
    e.exhaust = data && (data->exhaust || data->ethereal);   // 廃棄カードは燃える演出
    m_playCardEffects.push_back(e);
}

// 燃やしたカード：使ったカードと同じく手札から真ん中へ大きく飛び、そこで燃える。使ったカードと重ならないよう右に並べる
void BattleUI::StartBurnFromHand(const CardData* data, int cardIndex, int slot)
{
    StartPlayCardEffectFromHand(data, cardIndex, 0.0f, false);
    PlayCardEffect& e = m_playCardEffects.back();
    e.exhaust = true;                                                            // 真ん中に止まって燃える
    e.offsetX = (slot + 1) * CardVisual::CARD_W * g_animTune.playScale * 1.05f;   // 使ったカードの右から順に
}

// 燃やしたカードが真ん中で燃え上がるまで（真ん中で止まる＋燃え始めて半分）
float BattleUI::BurnShowTime()
{
    return PLAY_EFFECT_DUR * 0.72f + EXHAUST_BURN_DUR * 0.5f;
}

void BattleUI::GetPlayEffectTransform(const PlayCardEffect& e, float& x, float& y, float& scale, float& rot)
{
    float tx = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f + e.offsetX;   // 燃やしたカードは右に並ぶ
    float ty = m_screenHeight / 2.0f - CardVisual::CARD_H / 2.0f + g_animTune.playCenterY;

    float et = e.timer - e.delay;   // 遅延を反映した実時間
    if (et < 0.0f) et = 0.0f;

    if (e.isBurn)
    {
        float t = min(1.0f, et / BURN_EFFECT_DUR);
        float posT, scl, rt;
        if (t < 0.30f) {                                   // 接近：手札scale/rotから
            float u = t / 0.30f; float eo = 1.0f - (1.0f - u) * (1.0f - u);
            posT = eo;
            scl = e.startScale + (1.55f - e.startScale) * eo;   // 手札→1.55
            rt = e.startRot * (1.0f - eo);                      // 回転→0
        }
        else if (t < 0.58f) {                            // 中央でためる
            posT = 1.0f; float u = (t - 0.30f) / 0.28f; scl = 1.55f - 0.15f * u; rt = 0.0f;
        }
        else {                                           // 退場：手札サイズへ戻しつつフェード
            posT = 1.0f; float u = (t - 0.58f) / 0.42f;
            scl = 1.40f + (e.startScale - 1.40f) * u;      // 1.40 → 手札サイズ（小さくならない）
            rt = 0.0f;
        }
        float bx = e.startX + (tx - e.startX) * posT;
        float by = e.startY + (ty - e.startY) * posT;
        float w = CardVisual::CARD_W * scl, h = CardVisual::CARD_H * scl;
        x = bx - (w - CardVisual::CARD_W) / 2.0f;
        y = by - (h - CardVisual::CARD_H) / 2.0f;
        scale = scl; rot = rt;
        return;
    }

    // 通常カード：接近＋タメ(PLAY_EFFECT_DUR) → 退場はターン終了と同じ時間/軌道
    float holdEnd = g_animTune.playHold * PLAY_EFFECT_DUR;         // 接近+タメの終わり
    XMFLOAT2 dc = PileFx::PileCenter(PileFx::Pile::Discard, (float)m_screenWidth, (float)m_screenHeight);
    float dtx = dc.x - CardVisual::CARD_W * 0.5f, dty = dc.y - CardVisual::CARD_H * 0.5f;   // 捨て札の真ん中にカードの真ん中が着く
    float bx, by, scl; float outRot = 0.0f;
    if (et < holdEnd) {
        float t = et / PLAY_EFFECT_DUR;
        if (t < g_animTune.playApproach) {
            float u = t / g_animTune.playApproach;
            float eo = 1.0f - (1.0f - u) * (1.0f - u);
            bx = e.startX + (tx - e.startX) * eo;
            by = e.startY + (ty - e.startY) * eo;
            scl = 1.0f + (g_animTune.playScale - 1.0f) * eo;
        }
        else { bx = tx; by = ty; scl = g_animTune.playScale; }
    }
    else {
        float u = min(1.0f, (et - holdEnd) / DISCARD_EFFECT_DUR);
        if (e.exhaust) {          // 廃棄：中央に留まって溶ける）
            bx = tx; by = ty;
            scl = g_animTune.playScale;
        }
        else {                                  // 通常：小さくなって光に溶けながら捨て札へ（光の帯と同じ道）
            XMFLOAT2 p = PileFx::PathAt({ tx, ty }, { dtx, dty }, g_animTune.playArc, u);
            bx = p.x; by = p.y;
            scl = g_animTune.playScale * (1.0f - 0.75f * u);
        }
    }
    float w = CardVisual::CARD_W * scl, h = CardVisual::CARD_H * scl;
    x = bx - (w - CardVisual::CARD_W) / 2.0f;
    y = by - (h - CardVisual::CARD_H) / 2.0f;
    scale = scl; rot = outRot;
}

void BattleUI::DrawPlayCardEffects()
{
    for (auto& e : m_playCardEffects)
    {
        float x, y, s, rot;
        GetPlayEffectTransform(e, x, y, s, rot);
        float baseX = x + (CardVisual::CARD_W * s - CardVisual::CARD_W) / 2.0f;
        float baseY = y + (CardVisual::CARD_H * s - CardVisual::CARD_H) / 2.0f;
        XMFLOAT4 color = CardVisual::GetCardColor(e.cardType, false);
        if (e.exhaust) { color.x *= 0.4f; color.y *= 0.4f; color.z *= 0.45f; }
        color.w = e.alpha;
        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, baseX, baseY, s, rot, color, e.data, 0.0f);
    }
}

void BattleUI::DrawPlayCardEffectsFull(const BattleUIContext& ctx)
{
    // 呼び出し時はスプライトパス中
    for (auto& e : m_playCardEffects)
    {
        float x, y, s, rot;
        GetPlayEffectTransform(e, x, y, s, rot);
        float baseX = x + (CardVisual::CARD_W * s - CardVisual::CARD_W) / 2.0f;
        float baseY = y + (CardVisual::CARD_H * s - CardVisual::CARD_H) / 2.0f;

        float et = e.timer - e.delay;
        float holdEnd = 0.72f * PLAY_EFFECT_DUR;
        if (e.exhaust && et > holdEnd)                 // 廃棄：中央で文字ごと焼けて溶ける
        {
            DrawCardBurning(ctx, e.data, e.cardType, baseX, baseY, s, rot,
                min(1.0f, (et - holdEnd) / EXHAUST_BURN_DUR));
        }
        else
        {
            XMFLOAT4 color = CardVisual::GetCardColor(e.cardType, false); color.w = e.alpha;
            CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, baseX, baseY, s, rot, color, e.data, 0.0f);
            m_spriteRenderer->End(); m_textRenderer->Begin();
            CardVisual::DrawTexts(m_textRenderer, e.data, ctx.player, baseX, baseY, s, rot, e.alpha);
            m_textRenderer->End(); m_spriteRenderer->Begin();
        }
    }
}

void BattleUI::StartDiscardEffectAt(int cardIndex, const CardData* data, float delay, bool exhaust)
{
    if (cardIndex < 0 || cardIndex >= (int)m_cardAnims.size()) return;
    DiscardCardEffect e;
    e.startX = m_cardAnims[cardIndex].currentX;
    e.startY = m_cardAnims[cardIndex].currentY;
    e.startScale = m_cardAnims[cardIndex].currentScale;
    e.startRot = m_cardAnims[cardIndex].currentRot;
    e.alpha = 1.0f; e.timer = 0.0f; e.done = false;
    e.cardType = data ? data->type : CardType::Skill;
    e.data = data;
    e.delay = delay;
    e.exhaust = exhaust;
    m_discardCardEffects.push_back(e);
}

void BattleUI::StartBurnDiscard(const CardData* data, float delay)
{
    DiscardCardEffect e;
    e.startX = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
    e.startY = m_screenHeight / 2.0f - CardVisual::CARD_H / 2.0f;   // 画面中央
    e.alpha = 1.0f; e.timer = 0.0f; e.done = false;
    e.cardType = data ? data->type : CardType::Status;
    e.delay = delay; e.fromCenter = true;
    m_discardCardEffects.push_back(e);
}

void BattleUI::SpawnExhaustEmbers(float cx, float cy, float w, float h)
{
    int N = 30;
    for (int i = 0; i < N; i++)
    {
        UiEmber p;
        p.x = cx + ((float)rand() / RAND_MAX - 0.5f) * w;
        p.y = cy + ((float)rand() / RAND_MAX - 0.5f) * h;
        p.vx = ((float)rand() / RAND_MAX - 0.5f) * 50.0f;
        p.vy = -40.0f - (float)rand() / RAND_MAX * 80.0f;    // 上へ舞う
        p.maxLife = 0.45f + (float)rand() / RAND_MAX * 0.55f;
        p.life = p.maxLife;
        p.size = 3.0f + (float)rand() / RAND_MAX * 5.0f;
        m_exhaustEmbers.push_back(p);
    }
}

void BattleUI::UpdateExhaustEmbers(float dt)
{
    for (auto& p : m_exhaustEmbers)
    {
        p.life -= dt;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.vy += 30.0f * dt;            // 重力で上昇が鈍る
        p.vx *= (1.0f - 1.2f * dt);
    }
    m_exhaustEmbers.erase(
        std::remove_if(m_exhaustEmbers.begin(), m_exhaustEmbers.end(),
            [](const UiEmber& p) { return p.life <= 0.0f; }),
        m_exhaustEmbers.end());
}

void BattleUI::DrawExhaustEmbers()
{
    auto tex = TextureManager::Get("particle");
    for (auto& p : m_exhaustEmbers)
    {
        float a = p.life / p.maxLife;                        // 1→0
        XMFLOAT4 c(1.0f, 0.35f + 0.5f * a, 0.08f * a, a);    // 黄橙→赤→消
        float s = p.size * (0.3f + 0.7f * a);
        m_spriteRenderer->DrawSprite(tex ? tex : m_whiteTexture,
            p.x - s / 2, p.y - s / 2, s, s, 0.0f, c);
    }
}

void BattleUI::DrawDissolveCard(float baseX, float baseY, float scale,
    const CardData* data, CardType type, float progress)
{
    float x, y, w, h; CardVisual::GetRect(baseX, baseY, scale, x, y, w, h);
    XMFLOAT4 body = CardVisual::GetCardColor(type, false);
    XMFLOAT4 frameCol(0.6f, 0.6f, 0.58f, 1.0f);
    auto tex = TextureManager::Get("ui_card");

    const int   STR = 48;              // 分割を細かく（段差を消す）
    const float FEATHER = 0.28f;       // ぼかし幅（カード高さ比）＝グラデの厚み
    float front = progress * (1.0f + FEATHER) - FEATHER;   // 燃え際（下→上へ移動）
    float fw = 4.0f * scale;

    for (int s = 0; s < STR; s++)
    {
        float fc = ((float)s + 0.5f) / STR;      // 0=下端 .. 1=上端
        float u = (fc - front) / FEATHER;       // <0:消滅 / 0..1:グラデ / >1:健在
        float a = (u <= 0.0f) ? 0.0f
            : (u >= 1.0f) ? 1.0f
            : (u * u * (3.0f - 2.0f * u));   // smoothstep（なめらか）
        if (a <= 0.003f) continue;

        float glow = (u > 0.0f && u < 1.0f) ? (1.0f - u) : 0.0f;   // 燃え際を橙に
        float vTop = 1.0f - (float)(s + 1) / STR;
        float vBot = 1.0f - (float)s / STR;
        float sy = y + h * vTop;
        float sh = h / STR + 1.0f;                // 隙間防止に少し重ねる

        XMFLOAT4 fc4 = frameCol; fc4.w = a;
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, sy, w, sh, 0.0f, fc4);

        XMFLOAT4 bc = body;
        bc.x = min(1.0f, bc.x + glow * 0.9f);
        bc.y = bc.y * (1.0f - 0.4f * glow) + glow * 0.45f;
        bc.z = bc.z * (1.0f - 0.6f * glow);
        bc.w = a;
        m_spriteRenderer->DrawSprite(tex ? tex : m_whiteTexture, x + fw, sy, w - fw * 2.0f, sh,
            0.0f, bc, XMFLOAT4(0.0f, vTop, 1.0f, vBot));
    }
}
