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

// カードを引く・山札を戻す・カードが加わる 演出
// （BattleUI のメンバー。BattleUI.cpp から機能ごとに分けたファイル）

void BattleUI::StartDrawCardEffect(const std::string& cardId)
{
    DrawCardEffect effect;   // 引く音は動き出した時に鳴らす（UpdateDrawCardEffects）
    effect.cardId = cardId;
    XMFLOAT2 dp = PileFx::PilePos(PileFx::Pile::Draw, (float)m_screenWidth, (float)m_screenHeight);
    effect.x = dp.x;
    effect.y = dp.y;
    effect.targetX = m_screenWidth / 2.0f;
    effect.targetY = m_screenHeight - CardVisual::CARD_HIDE_Y_OFFSET;
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    m_drawCardEffects.push_back(effect);
}

void BattleUI::StartReshuffleEffect(int count)
{
    PileFx::Shuffle(count, (float)m_screenWidth, (float)m_screenHeight);   // 右下の捨て札から左下の山札へ、枚数分の光の帯
}

void BattleUI::HoldNewestDraws(int n, float t, float step)
{
    if (n <= 0) return;
    // 古い順に t, t+step, …。予約が重なった時（効果のドロー＋リシャッフル）は長いほう
    if ((int)m_pendingHolds.size() < n) m_pendingHolds.insert(m_pendingHolds.begin(), n - m_pendingHolds.size(), 0.0f);
    int base = (int)m_pendingHolds.size() - n;
    for (int k = 0; k < n; k++)
        m_pendingHolds[base + k] = max(m_pendingHolds[base + k], t + k * step);
    int j = 0;   // 飛ぶ演出も同じだけ待つ（新しい順に後ろから）
    for (auto it = m_drawCardEffects.rbegin(); it != m_drawCardEffects.rend() && j < n; ++it, ++j)
        it->delay = max(it->delay, t + (n - 1 - j) * step);
}

void BattleUI::StartEffectDraws(const std::vector<std::string>& ids)
{
    for (auto& id : ids) StartDrawCardEffect(id);
    HoldNewestDraws((int)ids.size(), EFFECT_DRAW_DELAY, EFFECT_DRAW_STEP);
}

void BattleUI::UpdateDrawCardEffects(float deltaTime)
{
    for (auto& effect : m_drawCardEffects)
    {
        if (effect.done) continue;
        if (effect.delay > 0.0f) { effect.delay -= deltaTime; continue; }
        if (!effect.started)
        {
            effect.started = true; Audio::PlaySE("Assets/Sound/se/draw.mp3");
            effect.startX = effect.x; effect.startY = effect.y;   // 動き出す位置（山札）
            PileFx::Bounce(PileFx::Pile::Draw);                    // 山札が弾む（ドローは光の帯を出さない）
        }

        effect.timer += deltaTime;
        float t = min(1.0f, effect.timer / DRAW_EFFECT_DURATION);

        float ease = 1.0f - (1.0f - t) * (1.0f - t);

        effect.x = effect.startX + (effect.targetX - effect.startX) * ease;
        effect.y = effect.startY + (effect.targetY - effect.startY) * ease;
        effect.alpha = 1.0f - t;

        if (t >= 1.0f) effect.done = true;
    }

    m_drawCardEffects.erase(
        std::remove_if(m_drawCardEffects.begin(), m_drawCardEffects.end(),
            [](const DrawCardEffect& e) { return e.done; }),
        m_drawCardEffects.end()
    );
}

void BattleUI::DrawCardEffects()
{
    for (auto& effect : m_drawCardEffects)
    {
        if (effect.delay > 0.0f) continue;   // まだ待機中
        const CardData* d = CardDataBase::Get(effect.cardId);
        XMFLOAT4 color = d ? CardVisual::GetCardColor(d->type, false)
            : XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);
        color.w = effect.alpha;

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
            effect.x, effect.y, 1.0f, 0.0f, color, d, 0.0f);
    }
}

void BattleUI::StartAddedCardEffect(const CardData* data, const std::string& target, int idx, int count)
{
    AddedCardEffect e;
    e.data = data;
    e.cardType = data ? data->type : CardType::Status;

    float spacing = CardVisual::CARD_W * 1.5f;                 // 枚数ぶん横に並べる
    float totalW = spacing * (count - 1);
    float cx = m_screenWidth / 2.0f - CardVisual::CARD_W / 2.0f;
    e.sx = cx - totalW / 2.0f + idx * spacing;
    e.sy = m_screenHeight * 0.40f;

    float sw = (float)m_screenWidth, sh = (float)m_screenHeight;
    if (target == "deck")         { XMFLOAT2 p = PileFx::PilePos(PileFx::Pile::Draw, sw, sh);    e.tx = p.x; e.ty = p.y; }   // 山札
    else if (target == "discard") { XMFLOAT2 p = PileFx::PilePos(PileFx::Pile::Discard, sw, sh); e.tx = p.x; e.ty = p.y; }   // 捨て札
    else { e.tx = cx; e.ty = sh - 60.0f; }   // hand(保険)

    m_addedCardEffects.push_back(e);
}

void BattleUI::UpdateAddedCardEffects(float dt)
{
    for (auto& e : m_addedCardEffects)
    {
        e.timer += dt;
        if (e.timer >= ADD_HOLD + ADD_FLY) e.done = true;
    }
    m_addedCardEffects.erase(
        std::remove_if(m_addedCardEffects.begin(), m_addedCardEffects.end(),
            [](const AddedCardEffect& e) { return e.done; }),
        m_addedCardEffects.end());
}

void BattleUI::DrawAddedCardEffectsFull(const BattleUIContext& ctx)
{
    for (auto& e : m_addedCardEffects)
    {
        float x = e.sx, y = e.sy, s = 1.0f, a = 1.0f;
        if (e.timer < ADD_HOLD)
        {
            float u = min(1.0f, e.timer / 0.15f);
            s = 0.9f + 0.5f * u;
        }
        else
        {
            float u = min(1.0f, (e.timer - ADD_HOLD) / ADD_FLY);
            float ease = u * u;
            x = e.sx + (e.tx - e.sx) * ease;
            y = e.sy + (e.ty - e.sy) * ease - sinf(u * 3.14159f) * 40.0f;
            s = 1.4f - 0.9f * u;
            a = 1.0f - u * 0.9f;
        }
        XMFLOAT4 c = CardVisual::GetCardColor(e.cardType, false); c.w = a;
        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, x, y, s, 0.0f, c, e.data, 0.0f);
        m_spriteRenderer->End(); m_textRenderer->Begin();
        CardVisual::DrawTexts(m_textRenderer, e.data, ctx.player, x, y, s, 0.0f, a);
        m_textRenderer->End(); m_spriteRenderer->Begin();
    }
}

void BattleUI::DrawAddedCardsTop(const BattleUIContext& ctx)
{
    if (m_addedCardEffects.empty()) return;
    m_spriteRenderer->Begin();
    DrawAddedCardEffectsFull(ctx);
    m_spriteRenderer->End();
}
