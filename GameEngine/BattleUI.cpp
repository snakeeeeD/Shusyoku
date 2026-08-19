#include "BattleUI.h"
#include "BuffInfo.h"
#include "CardExecutor.h"
#include "CardVisual.h"
#include "CardTooltip.h"
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
#include "GameUtils.h"
#include "Audio.h"
#include <algorithm>

using namespace DirectX;

BattleUI::~BattleUI()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
}

static std::string s_hoverKeyB;
static bool UiHoverB(float x, float y, float w, float h, POINT mp, const char* key)
{
    bool over = mp.x >= x && mp.x <= x + w && mp.y >= y && mp.y <= y + h;
    if (over && s_hoverKeyB != key) { Audio::PlaySE("Assets/Sound/se/hover.mp3"); s_hoverKeyB = key; }
    else if (!over && s_hoverKeyB == key) s_hoverKeyB.clear();
    return over;
}

bool BattleUI::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight))
        return false;

    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");
    return true;
}

void BattleUI::GridToWorld(GridMap* gridMap, int col, int row, float& outX, float& outZ)
{
    outX = (col - gridMap->GetCols() / 2.0f + 0.5f) * 1.1f;
    outZ = (row - gridMap->GetRows() / 2.0f + 0.5f) * 1.1f;
}

void BattleUI::DrawHPBar(float x, float y, float w, float h, const HPBarInfo& info, float time)
{
    float hpRatio = (float)info.currentHP / (float)info.maxHP;
    float displayRatio = info.displayHP / (float)info.maxHP;
    float poisonRatio = (float)info.poisonDmg / (float)info.maxHP;

    // ブロック時：外枠を青白く
    if (info.block > 0)
    {
        float glow = 0.7f + 0.3f * sin(time * 3.0f);
        XMFLOAT4 glowColor = XMFLOAT4(0.7f * glow, 0.8f * glow, 1.0f * glow, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x - 5.0f, y - 5.0f, w + 10.0f, h + 10.0f,
            0.0f, glowColor);
    }

    // 黒い外枠
    m_spriteRenderer->DrawSprite(m_whiteTexture, x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f,
        0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
    // 背景（暗い赤）
    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    // 減少アニメーション（オレンジ）
    if (displayRatio > hpRatio)
    {
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w * displayRatio, h,
            0.0f, XMFLOAT4(0.9f, 0.6f, 0.1f, 1.0f));
    }

    // HPバー本体
    XMFLOAT4 barColor;
    // ブロック時：青白い外枠
    if (info.block > 0)
    {
         barColor = XMFLOAT4(0.3f, 0.5f , 1.0f, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w * hpRatio, h,
            0.0f, barColor);
    }
    else if (info.hasBurn)
    {
        float flash = 0.5f + 0.5f * sin(time * 4.0f);
        barColor = XMFLOAT4(0.8f, 0.4f * flash, 0.0f, 1.0f);
    }
    else
        barColor = XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f);

    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w * hpRatio, h,
        0.0f, barColor);

    // 毒ダメージ予測（紫）
    if (info.poisonDmg > 0 && hpRatio > 0.0f)
    {
        float poisonW = w * min(poisonRatio, hpRatio);
        float poisonX = x + w * hpRatio - poisonW;
        float alpha = 0.6f + 0.4f * sin(time * 4.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, poisonX, y, poisonW, h,
            0.0f, XMFLOAT4(0.5f * alpha, 0.0f, 0.8f * alpha, alpha));
    }
}

void BattleUI::DrawEnemyHPBar(Enemy* enemy, Renderer3D* renderer3D)
{
    float screenX, screenY;
    if (!GetEnemyScreenPos(enemy, renderer3D, screenX, screenY)) return;

    float barWidth = enemy->IsBoss() ? 150.0f : 100.0f;
    float barHeight = enemy->IsBoss() ? 20.0f : 16.0f;
    float barX = screenX - barWidth / 2.0f;
    float barY = screenY - barHeight;

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f,
        0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX, barY, barWidth, barHeight,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    float ratio = (float)enemy->GetHp() / (float)enemy->GetMaxHp();
    XMFLOAT4 barColor = ratio > 0.5f
        ? XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f)
        : ratio > 0.25f
        ? XMFLOAT4(0.8f, 0.8f, 0.0f, 1.0f)
        : XMFLOAT4(0.8f, 0.0f, 0.0f, 1.0f);

    m_spriteRenderer->DrawSprite(m_whiteTexture,
        barX, barY, barWidth * ratio, barHeight,
        0.0f, barColor);
}

bool BattleUI::GetEnemyScreenPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const
{
    float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
    return WorldToScreen(enemy->worldX,
        enemy->worldY + enemy->height * cos(pitch),
        enemy->worldZ + 0.5f - enemy->height * sin(pitch),
        renderer3D, outX, outY);
}

bool BattleUI::GetEnemyFootPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const
{
    return WorldToScreen(enemy->worldX, 0.0f, enemy->worldZ + 0.5f, renderer3D, outX, outY);
}

void BattleUI::Draw(const BattleUIContext& ctx)
{
    const float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    const float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;
    const auto& cards = ctx.hand->GetCards();

    m_hasHoveredBuff = false;

    m_spriteRenderer->Begin();
    HPBarInfo playerBar;
    playerBar.currentHP = ctx.player->GetHp();
    playerBar.maxHP = ctx.player->GetMaxHp();
    playerBar.displayHP = ctx.player->GetDisplayHp();
    playerBar.block = ctx.player->GetBlock();
    playerBar.poisonDmg = ctx.player->GetBuffManager().GetTurnEndDamage().total();
    playerBar.hasBurn = ctx.player->GetBuffManager().HasBuff(BuffType::Burn);
    DrawHPBar(20.0f, 140.0f, 200.0f, 30.0f, playerBar, ctx.highlightTimer);


    if (ctx.player->GetBlock() > 0)
    {
        float pIconSize = 30.0f * 1.5f;
        float pIconX = 20.0f - pIconSize * 0.35f;
        float pIconY = 110.0f + (30.0f - pIconSize) / 2.0f;
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            pIconX - 1.0f, pIconY - 1.0f, pIconSize + 2.0f, pIconSize + 2.0f,
            0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            pIconX, pIconY, pIconSize, pIconSize,
            0.0f, XMFLOAT4(0.3f, 0.6f, 1.0f, 1.0f));
    }

    {
        float ex = 20.0f, ey = 190.0f, es = 60.0f;
        m_spriteRenderer->DrawSprite(m_whiteTexture, ex - 3, ey - 3, es + 6, es + 6, 0.0f, XMFLOAT4(0.15f, 0.10f, 0.0f, 1.0f)); // 縁
        m_spriteRenderer->DrawSprite(m_whiteTexture, ex, ey, es, es, 0.0f, XMFLOAT4(0.95f, 0.72f, 0.12f, 1.0f));               // 本体（金）
    }

    int topIdx = (ctx.selectedCardIndex >= 0) ? ctx.selectedCardIndex : ctx.hoveredCardIndex;

    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float drawPileW = 50.0f;
    float drawPileH = 40.0f;
    bool hoverDrawPile = UiHoverB(drawPileX, drawPileY, drawPileW, drawPileH, ctx.mousePos, "drawpile");
    float dpy = hoverDrawPile ? drawPileY - 6.0f : drawPileY;
    XMFLOAT4 drawPileColor = hoverDrawPile ? XMFLOAT4(0.3f, 0.3f, 0.9f, 1.0f) : XMFLOAT4(0.2f, 0.2f, 0.6f, 1.0f);
    XMFLOAT4 dpTint = hoverDrawPile ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
    m_spriteRenderer->DrawSprite(TextureManager::Get("ui_draw"), drawPileX, dpy, drawPileW, drawPileH, 0.0f, dpTint);
    // 山札バッジ（209の直後）
    m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
        drawPileX + drawPileW - 24.0f, dpy + drawPileH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;
    float discardW = 50.0f;
    float discardH = 40.0f;
    bool hoverDiscard = UiHoverB(discardX, discardY, discardW, discardH, ctx.mousePos, "discardpile");
    float ddy = hoverDiscard ? discardY - 6.0f : discardY;
    XMFLOAT4 discardColor = hoverDiscard ? XMFLOAT4(0.8f, 0.3f, 0.3f, 1.0f) : XMFLOAT4(0.5f, 0.2f, 0.2f, 1.0f);
    XMFLOAT4 dcTint = hoverDiscard ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
    m_spriteRenderer->DrawSprite(TextureManager::Get("ui_discard"), discardX, ddy, discardW, discardH, 0.0f, dcTint);
    m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
        discardX + discardW - 24.0f, ddy + discardH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));

    if (ctx.deck->GetExhaustPileCount() > 0)
    {
        float exhaustX = 140.0f;
        float exhaustY = m_screenHeight - 60.0f;
        float exhaustW = 50.0f;
        float exhaustH = 40.0f;
        bool hoverExhaust = UiHoverB(exhaustX, exhaustY, exhaustW, exhaustH, ctx.mousePos, "exhaustpile");
        float epy = hoverExhaust ? exhaustY - 6.0f : exhaustY;
        XMFLOAT4 exhaustColor = hoverExhaust ? XMFLOAT4(0.6f, 0.6f, 0.3f, 1.0f) : XMFLOAT4(0.4f, 0.4f, 0.2f, 1.0f);
        XMFLOAT4 exTint = hoverExhaust ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.82f, 0.82f, 0.82f, 1);
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_exhaust"), exhaustX, epy, exhaustW, exhaustH, 0.0f, exTint);
        m_spriteRenderer->DrawSprite(TextureManager::Get("particle"),
            exhaustX + exhaustW - 24.0f, epy + exhaustH - 22.0f, 24.0f, 24.0f, 0.0f, XMFLOAT4(0.10f, 0.09f, 0.13f, 1.0f));
    }

    DrawPlayCardEffects();
    DrawDiscardEffects();

    m_spriteRenderer->End();

    m_spriteRenderer->Begin();
    DrawTargetIndicators(ctx);


    // 移動経路を先に全部計算（先に決まった移動先を占有扱いにしてかぶりを避ける）
    if (ctx.isPlayerTurn && ctx.enemies)
    {
        std::vector<std::pair<int, int>> claimed;
        for (int ei = 0; ei < (int)ctx.enemies->size(); ei++)
        {
            Enemy* e = (*ctx.enemies)[ei];
            if (e->GetHp() <= 0) { e->SetPlannedMovePath({}); continue; }

            std::vector<CellType> saved;
            for (auto& c : claimed) { saved.push_back(ctx.gridMap->GetCell(c.first, c.second).type); ctx.gridMap->SetCellType(c.first, c.second, CellType::Enemy); }
            auto path = e->PlannedMovePath(ctx.playerCol, ctx.playerRow, ctx.gridMap);
            for (int k = 0; k < (int)claimed.size(); k++) ctx.gridMap->SetCellType(claimed[k].first, claimed[k].second, saved[k]);

            e->SetPlannedMovePath(path);
            if (!path.empty()) claimed.push_back(path.back());
        }
    }

    if (ctx.showMoveArrows && ctx.enemies)
        for (int ei = 0; ei < (int)ctx.enemies->size(); ei++)
        {
            Enemy* enemy = (*ctx.enemies)[ei];
            if (enemy->GetHp() <= 0) continue;

            const auto& path = enemy->GetPlannedMovePath();
            if (path.empty()) continue;

            XMFLOAT4 hue = HighlightPalette::EnemyHue(ei);
            auto proj = [&](float wx, float wz, float& sx, float& sy) -> bool {
                XMVECTOR wp = XMVectorSet(wx - 0.5f, 0.15f, wz - 0.5f, 1.0f);
                XMVECTOR cp = XMVector4Transform(wp, ctx.renderer3D->GetViewMatrix() * ctx.renderer3D->GetProjectionMatrix());
                XMFLOAT4 clip; XMStoreFloat4(&clip, cp);
                if (clip.w <= 0) return false;
                sx = (clip.x / clip.w + 1) * 0.5f * m_screenWidth;
                sy = (1 - clip.y / clip.w) * 0.5f * m_screenHeight;
                return true;
                };

            float dwx, dwz; GridToWorld(ctx.gridMap, path.back().first, path.back().second, dwx, dwz);
            // 敵の見た目マス（Enemy座標系 → マス番号）が移動先に着いたら消す
            int evc = (int)lroundf(enemy->worldX / 1.1f + ctx.gridMap->GetCols() / 2.0f);
            int evr = (int)lroundf(enemy->worldZ / 1.1f + ctx.gridMap->GetRows() / 2.0f);
            if (evc == path.back().first && evr == path.back().second) continue;

            // 始点：プレイヤーターン中はマス中心。敵ターン中は見た目位置を GridToWorld 系に変換
            float swx, swz;
            if (ctx.isPlayerTurn)
                GridToWorld(ctx.gridMap, enemy->gridCol, enemy->gridRow, swx, swz);
            else
            {
                // 敵のworld → 連続グリッド座標 → GridToWorld系へ
                float fcol = enemy->worldX / 1.1f + ctx.gridMap->GetCols() / 2.0f;
                float frow = enemy->worldZ / 1.1f + ctx.gridMap->GetRows() / 2.0f;
                float w0x, w0z, w1x, w1z;
                GridToWorld(ctx.gridMap, 0, 0, w0x, w0z);
                GridToWorld(ctx.gridMap, 1, 1, w1x, w1z);
                swx = w0x + (w1x - w0x) * fcol;     // マス→ワールドの線形変換
                swz = w0z + (w1z - w0z) * frow;
            }
            float esx, esy; if (!proj(swx, swz, esx, esy)) continue;
            float dsx, dsy; if (!proj(dwx, dwz, dsx, dsy)) continue;

            float vx = dsx - esx, vy = dsy - esy;
            float L = sqrtf(vx * vx + vy * vy);
            float ang = atan2f(vy, vx);

            // 先端の三角
            float hw = 0.5f + 0.5f * sinf(ctx.highlightTimer * 2.5f - L * 0.035f);
            float hs = 40.0f + 8.0f * hw;
            m_spriteRenderer->DrawSprite(TextureManager::Get("ui_arrowhead"), dsx - hs / 2, dsy - hs / 2, hs, hs, ang,
                XMFLOAT4(hue.x, hue.y, hue.z, 0.75f + 0.25f * hw));

            // シャフト＝均等な破線（パーツが滑らかに大小して波打つ）
            const float headLen = 26.0f, step = 26.0f, baseLen = 16.0f, baseThick = 13.0f;
            float scroll = fmodf(ctx.highlightTimer * 50.0f, step);
            float breath = 0.5f + 0.5f * sinf(ctx.highlightTimer * 3.0f);   // ← 全体が一緒に脈動（整数倍＝ラップも滑らか）
            for (float d = 6.0f + scroll; d < L - headLen; d += step)
            {
                float cx = esx + (vx / L) * d;
                float cy = esy + (vy / L) * d;

                float dl = baseLen * (0.8f + 0.35f * breath);   // 時間で大小（全□同じ）
                float dt = baseThick * (0.8f + 0.35f * breath);

                float a = 1.0f;
                float fromStart = d - 6.0f, toHead = (L - headLen) - d;
                if (fromStart < step) a *= fromStart / step;
                if (toHead < step) a *= toHead / step;
                if (a < 0.0f) a = 0.0f;

                m_spriteRenderer->DrawSprite(m_whiteTexture, cx - dl / 2, cy - dt / 2, dl, dt, ang,
                    XMFLOAT4(hue.x, hue.y, hue.z, 0.85f * a));
            }
        }

    DrawPlayerOffScreenIndicator(ctx);
    if (ctx.discardSelectCount > 0)
    {
        if (!ctx.discardViewMode)
            m_spriteRenderer->DrawSprite(m_whiteTexture, 0.0f, 0.0f,
                (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0, 0, 0, 0.55f));

        int sel = ctx.discardSelected ? (int)ctx.discardSelected->size() : 0;
        bool ready = (sel == ctx.discardSelectCount);

        float x, y, w, h;
        GetDiscardConfirmRect(x, y, w, h);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h, 0.0f,
            ready ? XMFLOAT4(0.7f, 0.3f, 0.2f, 1.0f) : XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f));

        GetDiscardViewRect(x, y, w, h);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h, 0.0f,
            ctx.discardViewMode ? XMFLOAT4(0.3f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.3f, 0.3f, 0.35f, 1.0f));
    }
    m_spriteRenderer->End();
    m_textRenderer->Begin();
    DrawPlayCardEffectTexts(ctx);

    // 使用可能カードの発光（縁・パルス）を1枚描く
    auto drawPlayableGlow = [&](int i) {
        if (i < 0 || i >= (int)m_cardAnims.size()) return;
        if (cards[i]->GetData()->cost > ctx.player->GetEnergy()) return;
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

    // 手札：本体→文字 を1枚ずつ交互に描く
    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i >= (int)m_cardAnims.size()) continue;
        if (i == topIdx) continue;

        XMFLOAT4 col = CardVisual::GetCardColor(cards[i]->GetData()->type, false);   // ← 上書きしない（元の色）

        m_textRenderer->End();
        m_spriteRenderer->Begin();
        drawPlayableGlow(i);

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

        m_textRenderer->End();
        m_spriteRenderer->Begin();

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
            m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, m_cardAnims[i].currentRot,
            col,
            cards[i]->GetData(), ctx.highlightTimer);
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        CardVisual::DrawTexts(m_textRenderer, cards[i]->GetData(), ctx.player,
            m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, m_cardAnims[i].currentRot);
    }


    // 上に来るカードは、本体 → 文字 の順で最後に描く（下の文字を覆う）
    if (topIdx >= 0 && topIdx < (int)cards.size() && topIdx < (int)m_cardAnims.size())
    {
        m_textRenderer->End();
        m_spriteRenderer->Begin();
        drawPlayableGlow(topIdx);

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
            m_cardAnims[topIdx].currentScale, m_cardAnims[topIdx].currentRot);
    }


    wchar_t drawText[32];
    // 山札枚数（バッジ上・アウトライン）
    {
        wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetDrawPileCount());
        m_textRenderer->DrawOutlinedText(t, drawPileX + drawPileW - 18.0f, dpy + drawPileH - 20.0f, 17.0f,
            D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
    }

    wchar_t discardText[32];
    // 捨て札枚数（バッジ上・アウトライン）
    {
        wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetDiscardPileCount());
        m_textRenderer->DrawOutlinedText(t, discardX + discardW - 18.0f, ddy + discardH - 20.0f, 17.0f,
            D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
    }

    if (ctx.deck->GetExhaustPileCount() > 0)
    {
        float ex2 = 140.0f, ey2 = m_screenHeight - 60.0f;
        bool he = ctx.mousePos.x >= ex2 && ctx.mousePos.x <= ex2 + 50.0f
            && ctx.mousePos.y >= ey2 && ctx.mousePos.y <= ey2 + 40.0f;
        float ty = he ? ey2 - 6.0f : ey2;                       // ホバーで浮く分
        wchar_t t[16]; swprintf_s(t, L"%d", ctx.deck->GetExhaustPileCount());   // ← 廃棄札の枚数
        m_textRenderer->DrawOutlinedText(t, ex2 + 50.0f - 18.0f, ty + 40.0f - 20.0f, 17.0f,
            D2D1::ColorF(1, 1, 1), D2D1::ColorF(0, 0, 0), 2.0f);
    }

    // パイルのホバー説明ウィンドウ
    const wchar_t* pileTip = nullptr;
    if (hoverDrawPile) pileTip = L"山札：これから引くカード";
    else if (hoverDiscard)  pileTip = L"捨て札：使ったカード（尽きたら戻る）";
    else if (ctx.deck->GetExhaustPileCount() > 0
        && ctx.mousePos.x >= 140.0f && ctx.mousePos.x <= 190.0f
        && ctx.mousePos.y >= m_screenHeight - 60.0f && ctx.mousePos.y <= m_screenHeight - 20.0f)
        pileTip = L"廃棄：このバトル中は戻らない";

    if (pileTip)
    {
        float tw = 320.0f, th = 28.0f, tx = 20.0f, ty = m_screenHeight - 100.0f;
        m_textRenderer->End();                                  
        m_spriteRenderer->Begin();
        DrawWindow(tx - 6, ty - 6, tw + 12, th + 12);   // 枠分だけ広げてパディング確保
        m_spriteRenderer->End();
        m_textRenderer->Begin();                                 // テキスト再開（以降の描画はこのバッチで続く）
        m_textRenderer->DrawText(pileTip, tx + 10.0f, ty + 5.0f, 15.0f, D2D1::ColorF(1, 1, 1));
    }
    wchar_t hpText[64];
    swprintf_s(hpText, L"%d / %d", ctx.player->GetHp(), ctx.player->GetMaxHp());
    m_textRenderer->DrawOutlinedText(hpText, 55.0f, 90.0f, 45.0f, D2D1::ColorF(D2D1::ColorF::White));

    wchar_t energyText[64];
    {
        float ex = 20.0f, ey = 196.0f, es = 46.0f;
        wchar_t eCur[16]; swprintf_s(eCur, L"%d", ctx.player->GetEnergy());
        m_textRenderer->DrawText(eCur, ex + es / 2.0f - 11.0f, ey + 5.0f, 32.0f, D2D1::ColorF(0.15f, 0.08f, 0.0f)); // 大きく
        wchar_t eMax[16]; swprintf_s(eMax, L"/%d", ctx.player->GetMaxEnergy());
        m_textRenderer->DrawText(eMax, ex + es - 4.0f, ey + es - 20.0f, 15.0f, D2D1::ColorF(0.35f, 0.2f, 0.0f));
    }

    if (ctx.discardSelectCount > 0)
    {
        int sel = ctx.discardSelected ? (int)ctx.discardSelected->size() : 0;
        wchar_t msg[64];
        swprintf_s(msg, L"捨てるカードを選択 (%d/%d)", sel, ctx.discardSelectCount);
        m_textRenderer->DrawText(msg, m_screenWidth / 2.0f - 120.0f, m_screenHeight - 380.0f, 24.0f,
            D2D1::ColorF(1.0f, 0.8f, 0.3f));

        float x, y, w, h;
        GetDiscardConfirmRect(x, y, w, h);
        m_textRenderer->DrawText(L"確定", x + 45.0f, y + 8.0f, 20.0f, D2D1::ColorF(1, 1, 1));
        GetDiscardViewRect(x, y, w, h);
        m_textRenderer->DrawText(ctx.discardViewMode ? L"戻る" : L"盤面を見る",
            x + 20.0f, y + 8.0f, 20.0f, D2D1::ColorF(1, 1, 1));
    }

    if (ctx.player->GetBlock() > 0)
    {
        float pIconSize = 30.0f * 1.5f;
        float pIconX = 20.0f - pIconSize * 0.35f;
        float pIconY = 110.0f + (30.0f - pIconSize) / 2.0f;
        wchar_t blockText[16];
        swprintf_s(blockText, L"%d", ctx.player->GetBlock());
        float bFontSize = 18.0f;
        float bTextW = wcslen(blockText) * bFontSize * 0.5f;
        m_textRenderer->DrawText(blockText,
            pIconX + (pIconSize - bTextW) / 2.0f + 1.0f,
            pIconY + (pIconSize - bFontSize) / 2.0f + 1.0f,
            bFontSize, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
        m_textRenderer->DrawText(blockText,
            pIconX + (pIconSize - bTextW) / 2.0f,
            pIconY + (pIconSize - bFontSize) / 2.0f,
            bFontSize, D2D1::ColorF(D2D1::ColorF::White));
    }

    // 敵UI
        m_textRenderer->End();
        m_spriteRenderer->Begin();

        m_intentHover = false;

        for (auto enemy : *ctx.enemies)
        {
            float headX, headY, footX, footY;
            if (!GetEnemyScreenPos(enemy, ctx.renderer3D, headX, headY)) continue;
            if (!GetEnemyFootPos(enemy, ctx.renderer3D, footX, footY)) continue;

            float scale = 1.0f / ctx.cameraZoom;  // ズームアウト時に小さくなる

            float barWidth = (enemy->IsBoss() ? 100.0f : 50.0f) * scale;
            float barHeight = (enemy->IsBoss() ? 10.0f : 7.0f) * scale;

            // --- HPバー（足元の少し下） ---
            float barX = footX - barWidth / 2.0f;
            float barY = footY - 30.0f;

            // この敵の攻撃が今あなたに当たる → キャラの頭上に大きめの赤警告（敵の識別色フチ）
            if (enemy->hitsPlayer && ctx.isPlayerTurn)
            {
                float mk = 34.0f;   
                float mx = headX - mk / 2.0f;
                float my = headY - mk - 14.0f;
                float pulse = 0.75f + 0.25f * sinf(ctx.highlightTimer * 6.0f);
                const XMFLOAT4& hue = enemy->hueColor;
                m_spriteRenderer->DrawSprite(TextureManager::Get("ui_hitring"),
                    mx, my, mk, mk, 0.0f, XMFLOAT4(hue.x, hue.y, hue.z, pulse));   // 敵色フチ
                m_spriteRenderer->DrawSprite(TextureManager::Get("ui_hitmark"),
                    mx, my, mk, mk, 0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, pulse));       // 赤バッジ

                if (ctx.mousePos.x >= mx && ctx.mousePos.x <= mx + mk
                    && ctx.mousePos.y >= my && ctx.mousePos.y <= my + mk)
                {
                    m_intentHover = true;
                    m_intentX = mx + mk * 0.5f;
                    m_intentY = my;
                    m_intentTitle = L"攻撃が当たる！";
                    m_intentBody = L"今の位置はこの敵の攻撃範囲内\n移動して避けよう";
                }
            }

            float footX2, footY2;

            int trE = 1 + ctx.player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
            int ppE = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
            int psumE = 0; for (int i = 0; i < trE; i++) { int v = ppE - i; if (v <= 0) break; psumE += v; }

            HPBarInfo eBar;
            eBar.currentHP = enemy->GetHp(); eBar.maxHP = enemy->GetMaxHp();
            eBar.displayHP = enemy->GetDisplayHp(); eBar.block = enemy->GetBlock();
            eBar.poisonDmg = psumE; eBar.hasBurn = enemy->GetBuffManager().HasBuff(BuffType::Burn);

            DrawUnitStatusSprites(footX, footY, scale, enemy->IsBoss(), eBar,
                enemy->GetBuffManager(), ctx.mousePos, ctx.highlightTimer);

            // --- 次の行動アイコン（頭上） ---
            {
                float iconSize = EnemyIntentVisual::ICON_SIZE;
                float iconX = barX;
                float iconY = barY - iconSize - 2.0f;

                for (auto& act : enemy->GetPlannedActions())
                    for (auto& e : act.effects)
                    {
                        if (!EnemyIntentVisual::ShouldShow(e)) continue;

                        bool hov = (ctx.mousePos.x >= iconX && ctx.mousePos.x <= iconX + iconSize
                            && ctx.mousePos.y >= iconY && ctx.mousePos.y <= iconY + iconSize);
                        if (hov)
                        {
                            float p = 3.0f + 2.0f * (0.5f + 0.5f * sinf(ctx.highlightTimer * 6.0f));
                            m_spriteRenderer->DrawSprite(m_whiteTexture, iconX - p, iconY - p,
                                iconSize + p * 2, iconSize + p * 2, 0.0f, XMFLOAT4(1.0f, 0.85f, 0.35f, 0.9f));
                            m_intentHover = true;
                            m_intentX = iconX + iconSize * 0.5f;
                            m_intentY = iconY;
                            EnemyIntentVisual::GetEffectDesc(e, act, enemy->GetBuffManager(),
                                m_intentTitle, m_intentBody);
                        }

                        EnemyIntentVisual::DrawIcon(m_spriteRenderer, m_whiteTexture, e,
                            iconX, iconY, EnemyIntentVisual::ICON_SIZE);
                        iconX += EnemyIntentVisual::STEP;
                    }
            }

            {
                float footX, footY;
                if (WorldToScreen(ctx.player->worldX, 0.0f, ctx.player->worldZ + 0.5f, ctx.renderer3D, footX, footY))
                {
                    float scale = 1.0f / ctx.cameraZoom;
                    HPBarInfo pBar;
                    pBar.currentHP = ctx.player->GetHp(); pBar.maxHP = ctx.player->GetMaxHp();
                    pBar.displayHP = ctx.player->GetDisplayHp(); pBar.block = ctx.player->GetBlock();
                    pBar.poisonDmg = ctx.player->GetBuffManager().GetTurnEndDamage().total();
                    pBar.hasBurn = ctx.player->GetBuffManager().HasBuff(BuffType::Burn);

                    m_spriteRenderer->Begin();
                    DrawUnitStatusSprites(footX, footY, scale, false, pBar,
                        ctx.player->GetBuffManager(), ctx.mousePos, ctx.highlightTimer);
                    m_spriteRenderer->End();

                    m_textRenderer->Begin();
                    DrawUnitStatusText(footX, footY, scale, false, pBar, ctx.player->GetBuffManager());
                    m_textRenderer->End();
                }
            }
        }

        m_spriteRenderer->End();
        m_textRenderer->Begin();

        for (auto enemy : *ctx.enemies)
        {
            float headX, headY, footX, footY;
            if (!GetEnemyScreenPos(enemy, ctx.renderer3D, headX, headY)) continue;
            if (!GetEnemyFootPos(enemy, ctx.renderer3D, footX, footY)) continue;

            float scale = 1.0f / ctx.cameraZoom;
            float barWidth = (enemy->IsBoss() ? 100.0f : 50.0f) * scale;
            float barHeight = (enemy->IsBoss() ? 15.0f : 10.0f) * scale;
            float barX = footX - barWidth / 2.0f;
            float barY = footY - 30.0f;
            float fontSize = max(8.0f, (enemy->IsBoss() ? 10.0f : 8.0f) * scale);

            HPBarInfo eBar;
            eBar.currentHP = enemy->GetHp(); eBar.maxHP = enemy->GetMaxHp();
            eBar.block = enemy->GetBlock();
            DrawUnitStatusText(footX, footY, scale, enemy->IsBoss(), eBar, enemy->GetBuffManager());

            // 行動の数値（頭上）
            const EnemyAction* action = enemy->GetNextAction();
            // 行動の数値（複合行動はすべて並べる）
            {
                float iconSize = EnemyIntentVisual::ICON_SIZE;
                float ix = barX;
                float iy = barY - iconSize - 2.0f;

                for (auto& act : enemy->GetPlannedActions())
                    for (auto& e : act.effects)
                    {
                        if (!EnemyIntentVisual::ShouldShow(e)) continue;

                        if (EnemyIntentVisual::HasValue(e))
                        {
                            int shownVal = EnemyIntentVisual::GetDisplayValue(e, enemy->GetBuffManager());
                            wchar_t buf[16];
                            swprintf_s(buf, L"%d", shownVal);

                            m_textRenderer->DrawText(buf, ix + iconSize + 4.0f, iy + 2.0f, 14.0f,
                                D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
                            m_textRenderer->DrawText(buf, ix + iconSize + 3.0f, iy + 1.0f, 14.0f,
                                D2D1::ColorF(D2D1::ColorF::White));
                        }

                        ix += EnemyIntentVisual::STEP;
                    }
            }

            // バフ/デバフ数値
            float buffIconY = barY + barHeight + 4.0f;
            float buffIconX = barX;
            float buffMaxX = barX + barWidth + 40.0f;
            for (auto& buff : enemy->GetBuffManager().GetBuffs())
            {
                float iconSize = 16.0f;
                if (buffIconX + iconSize + 20.0f > buffMaxX)
                {
                    buffIconX = barX;
                    buffIconY += iconSize + 6.0f;
                }
                wchar_t buffVal[16];
                if (BuffInfo::IsDurationBased(buff.type))
                    swprintf_s(buffVal, L"%dT", buff.duration);
                else
                    swprintf_s(buffVal, L"%d", buff.value);
                m_textRenderer->DrawText(buffVal,
                    buffIconX + iconSize + 2.0f, buffIconY + 1.0f,
                    11.0f, D2D1::ColorF(D2D1::ColorF::White));
                buffIconX += iconSize + 20.0f;
            }
        }

    const auto& buffs = ctx.player->GetBuffManager().GetBuffs();
    float buffY = 282.0f;
    for (auto& buff : buffs)
    {
        const auto& info = BuffInfo::Get(buff.type);
        bool buffHover = (ctx.mousePos.x >= 20.0f && ctx.mousePos.x <= 200.0f
            && ctx.mousePos.y >= buffY && ctx.mousePos.y <= buffY + 20.0f);
        // アイコン
        m_textRenderer->End();
        m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(m_whiteTexture, 20.0f, buffY, 16.0f, 16.0f, 0.0f,
            XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
        m_spriteRenderer->DrawSprite(m_whiteTexture, 21.0f, buffY + 1.0f, 14.0f, 14.0f, 0.0f,
            info.color);
        m_spriteRenderer->End();
        m_textRenderer->Begin();
        // 名前と値
        wchar_t buffText[64];
        if (buff.type == BuffType::Poison)
            swprintf_s(buffText, L"%s %d", info.name.c_str(), buff.value);
        else if (BuffInfo::IsDurationBased(buff.type))
            swprintf_s(buffText, L"%s %dターン", info.name.c_str(), buff.duration);
        else
            swprintf_s(buffText, L"%s %d (%dT)", info.name.c_str(), buff.value, buff.duration);
        D2D1::ColorF textColor = buffHover
            ? D2D1::ColorF(1.0f, 1.0f, 0.5f)
            : D2D1::ColorF(0.6f, 1.0f, 0.6f);
        m_textRenderer->DrawText(buffText, 40.0f, buffY, 14.0f, textColor);
        buffY += 20.0f;
        // ホバーで説明
        if (buffHover)
        {
            int descNum = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
            std::wstring desc = BuffInfo::GetDescription(buff.type, descNum);

            // 背景ウィンドウ
            float dw = 300.0f, dh = 20.0f;
            m_textRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(34.0f, buffY - 4.0f, dw + 8.0f, dh + 8.0f);
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            m_textRenderer->DrawText(desc.c_str(), 44.0f, buffY + 3.0f, 12.0f,
                D2D1::ColorF(0.85f, 0.85f, 0.85f));
            buffY += 22.0f;
        }
    }

    // ターンエンドボタン
    if (ctx.battleResult == BattleResult::None && ctx.isPlayerTurn)
    {
        float btnX = ctx.screenWidth - 160.0f;
        float btnY = ctx.screenHeight - 60.0f;
        float btnW = 140.0f;
        float btnH = 40.0f;

        bool hoverEnd = UiHoverB(btnX, btnY, btnW, btnH, ctx.mousePos, "turnend");
        float bey = hoverEnd ? btnY - 6.0f : btnY;
        XMFLOAT4 btnColor = hoverEnd ? XMFLOAT4(0.3f, 0.7f, 1.0f, 1.0f) : XMFLOAT4(0.2f, 0.5f, 0.8f, 1.0f);
        XMFLOAT4 teTint = hoverEnd ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.85f, 0.85f, 0.85f, 1);
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_turnend"), btnX, bey, btnW, btnH, 0.0f, teTint);
     
        if (hoverEnd) {
            float tx = btnX - 40.0f, ty = btnY - 40.0f, tw = 180.0f, th = 28.0f;
            m_spriteRenderer->End();
            m_spriteRenderer->Begin();
            DrawWindow(tx - 4.0f, ty - 4.0f, tw + 8.0f, th + 8.0f);
            m_textRenderer->DrawText(L"ターンを終了して敵の番へ", tx + 8.0f, ty + 5.0f, 15.0f, D2D1::ColorF(1, 1, 1));
        }
    }

    if (ctx.battleResult == BattleResult::Win)
    {
        const float cx = m_screenWidth / 2.0f;

        // ===== VICTORY バナー（布バナー＋アウトライン文字）=====
        m_spriteRenderer->End(); m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"),
            cx - 175.0f, m_screenHeight / 2.0f - 230.0f, 350.0f, 78.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));
        m_textRenderer->DrawOutlinedText(L"VICTORY!",
            cx - 120.0f, m_screenHeight / 2.0f - 214.0f, 46.0f,
            D2D1::ColorF(1.0f, 0.92f, 0.45f), D2D1::ColorF(0.35f, 0.16f, 0.0f), 3.0f);

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
                std::wstring rn, rd;
                if (auto r = RelicManager::Get(*ctx.rewardRelic)) { rn = ToWString(r->name); rd = ToWString(r->desc); }
                const float rw = 340.0f, rh = rd.empty() ? 40.0f : 66.0f, rrx = cx - rw / 2.0f;
                m_spriteRenderer->End(); m_spriteRenderer->Begin();
                DrawWindow(rrx, y, rw, rh);
                m_textRenderer->DrawOutlinedText((L"レリック獲得: " + rn).c_str(), rrx + 14.0f, y + 8.0f, 19.0f,
                    D2D1::ColorF(0.95f, 0.8f, 1.0f), D2D1::ColorF(0.2f, 0.1f, 0.25f), 2.5f);
                if (!rd.empty())
                    m_textRenderer->DrawText(rd.c_str(), rrx + 14.0f, y + 36.0f, 14.0f, D2D1::ColorF(0.86f, 0.86f, 0.92f));
            }
        }

        // ===== クリックで次へ（窓ピル）=====
        {
            const float pw = 210.0f, ph = 36.0f, px = cx - pw / 2.0f, py = m_screenHeight / 2.0f + 110.0f;
            DrawWindow(px, py, pw, ph);
            m_textRenderer->DrawOutlinedText(L"クリックで次へ", px + 34.0f, py + 8.0f, 20.0f,
                D2D1::ColorF(1, 1, 1), D2D1::ColorF(0.1f, 0.1f, 0.1f), 2.0f);
        }
    }
    else if (ctx.battleResult == BattleResult::Lose)
    {
        const float cx = m_screenWidth / 2.0f;
        m_spriteRenderer->End(); m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(TextureManager::Get("ui_banner"),
            cx - 190.0f, m_screenHeight / 2.0f - 40.0f, 380.0f, 78.0f, 0.0f, XMFLOAT4(0.75f, 0.5f, 0.5f, 1));
        m_textRenderer->DrawOutlinedText(L"GAME OVER",
            cx - 140.0f, m_screenHeight / 2.0f - 24.0f, 44.0f,
            D2D1::ColorF(1.0f, 0.42f, 0.42f), D2D1::ColorF(0.2f, 0.0f, 0.0f), 3.0f);
        const float pw = 250.0f, ph = 36.0f, px = cx - pw / 2.0f, py = m_screenHeight - 74.0f;
        m_spriteRenderer->End(); m_spriteRenderer->Begin();
        DrawWindow(px, py, pw, ph);
        m_textRenderer->DrawOutlinedText(L"クリックでタイトルへ", px + 26.0f, py + 8.0f, 20.0f,
            D2D1::ColorF(1, 1, 1), D2D1::ColorF(0.1f, 0.1f, 0.1f), 2.0f);
    }

    if (m_hasHoveredBuff)
    {
        const auto& info = BuffInfo::Get(m_hoveredBuffType);
        std::wstring desc = BuffInfo::GetDescription(m_hoveredBuffType, m_hoveredBuffValue);
        float tipX = m_hoveredBuffX;
        float tipY = m_hoveredBuffY - 40.0f;
        float tipW = 200.0f;
        float tipH = 36.0f;

        m_textRenderer->End();
        m_spriteRenderer->Begin();
        DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        m_textRenderer->DrawText(info.name.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
            D2D1::ColorF(1.0f, 1.0f, 0.5f));
        m_textRenderer->DrawText(desc.c_str(), tipX + 5.0f, tipY + 18.0f, 11.0f,
            D2D1::ColorF(0.8f, 0.8f, 0.8f));
    }

    // 敵インテントアイコンの説明ウィンドウ
    if (m_intentHover)
    {
        bool twoLines = (m_intentBody.find(L'\n') != std::wstring::npos);
        float tw = 220.0f, th = twoLines ? 64.0f : 46.0f;
        float tx = m_intentX - tw * 0.5f;
        float ty = m_intentY - th - 8.0f;
        if (tx < 4.0f) tx = 4.0f;
        if (tx + tw > m_screenWidth - 4.0f) tx = m_screenWidth - 4.0f - tw;
        if (ty < 4.0f) ty = m_intentY + 24.0f;   // 上に出せない時は下へ

        m_textRenderer->End();
        m_spriteRenderer->Begin();
        DrawWindow(tx, ty, tw, th);
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        m_textRenderer->DrawText(m_intentTitle.c_str(), tx + 10.0f, ty + 6.0f, 15.0f,
            D2D1::ColorF(1.0f, 0.85f, 0.35f));
        size_t nl = m_intentBody.find(L'\n');
        if (nl == std::wstring::npos)
            m_textRenderer->DrawText(m_intentBody.c_str(), tx + 10.0f, ty + 25.0f, 12.0f,
                D2D1::ColorF(0.9f, 0.9f, 0.9f));
        else
        {
            m_textRenderer->DrawText(m_intentBody.substr(0, nl).c_str(), tx + 10.0f, ty + 25.0f, 12.0f,
                D2D1::ColorF(0.9f, 0.9f, 0.9f));
            m_textRenderer->DrawText(m_intentBody.substr(nl + 1).c_str(), tx + 10.0f, ty + 42.0f, 12.0f,
                D2D1::ColorF(0.75f, 0.85f, 1.0f));   // 範囲行は青系
        }
    }

    // 罠のホバー詳細
    if (ctx.hoveredCell.first >= 0 && ctx.hoveredCell.second >= 0
        && ctx.hoveredCell.first < ctx.gridMap->GetCols()
        && ctx.hoveredCell.second < ctx.gridMap->GetRows())
    {
        auto& hCell = ctx.gridMap->GetCell(ctx.hoveredCell.first, ctx.hoveredCell.second);
        if (hCell.tileEffect.active)
        {
            const TerrainDef* def = TerrainDataBase::Get(hCell.tileEffect.id);
            if (def)
            {
                wchar_t detailText[64];
                if (def->effect == "Damage")
                    swprintf_s(detailText, L"ダメージ: %d", hCell.tileEffect.value);
                else if (def->effect == "Slide")
                    swprintf_s(detailText, L"移動方向に滑る");
                else
                {
                    BuffType bt = StringToBuffType(def->buffType);
                    const auto& info = BuffInfo::Get(bt);
                    swprintf_s(detailText, L"%s: %d (%dT)", info.name.c_str(), hCell.tileEffect.value, def->buffDuration);
                }

                float tipX = (float)ctx.mousePos.x + 15.0f;
                float tipY = (float)ctx.mousePos.y - 50.0f;
                float tipW = 150.0f;
                float tipH = 40.0f;

                m_textRenderer->End();
                m_spriteRenderer->Begin();
                DrawWindow(tipX - 4.0f, tipY - 4.0f, tipW + 8.0f, tipH + 8.0f);
                m_spriteRenderer->End();
                m_textRenderer->Begin();

                m_textRenderer->DrawText(def->name.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
                    D2D1::ColorF(def->color.x, def->color.y, def->color.z));
                m_textRenderer->DrawText(detailText, tipX + 5.0f, tipY + 20.0f, 11.0f,
                    D2D1::ColorF(D2D1::ColorF::LightGray));
            }
        }
    }

    if (ctx.showDrawPile || ctx.showDiscardPile || ctx.showExhaustPile)
    {
        m_textRenderer->End();
        DrawPileViewer(ctx);
        m_textRenderer->Begin();
    }

   DrawFloatingTexts(ctx);

    // ターンバナー（最前面：全ステータスの後に描く）
   if (TurnBanner::IsActive())
   {
       float a = TurnBanner::GetAlpha();
       float ox = TurnBanner::GetSlideX();
       TurnBannerType t = TurnBanner::GetType();

       const wchar_t* txt; D2D1::ColorF col(1, 1, 1, a); float chars; float charW; float spacing;
       if (t == TurnBannerType::BattleStart) { txt = L"戦闘開始";         col = D2D1::ColorF(1.0f, 0.9f, 0.4f, a); chars = 4.0f; charW = 50.0f; spacing = 14.0f; }
       else if (t == TurnBannerType::Player) { txt = L"プレイヤーターン"; col = D2D1::ColorF(0.4f, 0.85f, 1.0f, a); chars = 8.0f; charW = 30.0f; spacing = 18.0f; }
       else { txt = L"敵のターン";       col = D2D1::ColorF(1.0f, 0.4f, 0.35f, a); chars = 5.0f; charW = 44.0f; spacing = 18.0f; }

       float fh = 64.0f;
       float step = charW + spacing;
       float totalW = 0.0f;
       for (int ci = 0; ci < (int)chars; ci++)
       {
           totalW += (txt[ci] == L'ー') ? charW * 0.5f : charW;
           if (ci < (int)chars - 1) totalW += spacing;
       }
       float startX = m_screenWidth / 2.0f - totalW / 2.0f + ox;
       float y = m_screenHeight * 0.28f;
       float cx = startX;
       for (int ci = 0; ci < (int)chars; ci++)
       {
           wchar_t ch[2] = { txt[ci], L'\0' };
           float w = (txt[ci] == L'ー') ? charW * 0.5f : charW;
           m_textRenderer->DrawOutlinedText(ch, cx, y, fh, col, D2D1::ColorF(0, 0, 0, a), 2.5f);
           cx += w + spacing;
       }
   }

    m_textRenderer->End();

    m_spriteRenderer->Begin();
    DrawEnemyInfoPanel(ctx);

    if (ctx.hoveredCardIndex >= 0)
    {
        float cx, cy, cw, ch; GetCardRect(ctx.hoveredCardIndex, cx, cy, cw, ch);
        CardTooltip::Draw(m_spriteRenderer, m_textRenderer, m_whiteTexture,
            ctx.hand->GetCards()[ctx.hoveredCardIndex]->GetData(),
            cx + cw / 2.0f, cy, cw, ch, m_screenWidth, m_screenHeight);
    }
}

void BattleUI::DrawTargetIndicators(const BattleUIContext& ctx)
{
    if (ctx.selectedCardIndex < 0 || ctx.selectedCardIndex >= (int)ctx.hand->GetCards().size())
        return;

    const CardData* data = ctx.hand->GetCards()[ctx.selectedCardIndex]->GetData();
    if (!data) return;

    if (data->type == CardType::Attack)
    {
        XMFLOAT4 arrowColor(1.0f, 0.3f, 0.1f, 1.0f);

        if (data->dash && ctx.hoveredCell.first >= 0)
        {
            int dx = 0, dy = 0;
            if (ctx.hoveredCell.first > ctx.playerCol) dx = 1;
            else if (ctx.hoveredCell.first < ctx.playerCol) dx = -1;
            if (ctx.hoveredCell.second > ctx.playerRow) dy = 1;
            else if (ctx.hoveredCell.second < ctx.playerRow) dy = -1;

            if ((dx != 0) != (dy != 0))
            {
                int col = ctx.playerCol;
                int row = ctx.playerRow;
                int moveCol = ctx.playerCol;
                int moveRow = ctx.playerRow;
                Enemy* hitEnemy = nullptr;

                for (int step = 0; step < data->range; step++)
                {
                    col += dx;
                    row += dy;
                    if (col < 0 || col >= ctx.gridMap->GetCols()
                        || row < 0 || row >= ctx.gridMap->GetRows())
                        break;

                    for (auto enemy : *ctx.enemies)
                    {
                        for (auto& [ec, er] : enemy->GetGridShape())
                        {
                            if (enemy->gridCol + ec == col && enemy->gridRow + er == row)
                            {
                                hitEnemy = enemy;
                                goto dashFound;
                            }
                        }
                    }

                    if (ctx.gridMap->GetCell(col, row).type != CellType::Empty)
                        break;

                    moveCol = col;
                    moveRow = row;
                }
            dashFound:

                if (hitEnemy)
                {
                    // 敵に赤矢印
                    float sx, sy;
                    if (GetEnemyScreenPos(hitEnemy, ctx.renderer3D, sx, sy))
                        DrawArrowIndicator(sx, sy, XMFLOAT4(1.0f, 0.3f, 0.1f, 1.0f), ctx.highlightTimer);
                }

                // 移動先に緑矢印
                if (moveCol != ctx.playerCol || moveRow != ctx.playerRow)
                {
                    float wx, wz;
                    GridToWorld(ctx.gridMap, moveCol, moveRow, wx, wz);
                    XMVECTOR worldPos = XMVectorSet(wx - 0.5f, 0.5f, wz - 0.5f, 1.0f);
                    XMVECTOR clipPos = XMVector4Transform(worldPos,
                        ctx.renderer3D->GetViewMatrix() * ctx.renderer3D->GetProjectionMatrix());
                    XMFLOAT4 clip;
                    XMStoreFloat4(&clip, clipPos);
                    if (clip.w > 0.0f)
                    {
                        float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
                        float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
                        DrawArrowIndicator(sx, sy, XMFLOAT4(0.2f, 0.9f, 0.3f, 1.0f), ctx.highlightTimer);
                    }
                }
            }
            return;
        }

        if (data->pierce && ctx.hoveredCell.first >= 0)
        {
            int dx = 0, dy = 0;
            if (ctx.hoveredCell.first > ctx.playerCol) dx = 1;
            else if (ctx.hoveredCell.first < ctx.playerCol) dx = -1;
            if (ctx.hoveredCell.second > ctx.playerRow) dy = 1;
            else if (ctx.hoveredCell.second < ctx.playerRow) dy = -1;

            if ((dx != 0) != (dy != 0))
            {
                int col = ctx.playerCol;
                int row = ctx.playerRow;
                for (int step = 0; step < data->range; step++)
                {
                    col += dx;
                    row += dy;
                    if (col < 0 || col >= ctx.gridMap->GetCols()
                        || row < 0 || row >= ctx.gridMap->GetRows())
                        break;

                    if (ctx.gridMap->GetCell(col, row).type == CellType::Wall)
                        break;

                    for (auto enemy : *ctx.enemies)
                    {
                        for (auto& [ec, er] : enemy->GetGridShape())
                        {
                            if (enemy->gridCol + ec == col && enemy->gridRow + er == row)
                            {
                                float sx, sy;
                                if (GetEnemyScreenPos(enemy, ctx.renderer3D, sx, sy))
                                    DrawArrowIndicator(sx, sy, XMFLOAT4(1.0f, 0.3f, 0.1f, 1.0f), ctx.highlightTimer);
                            }
                        }
                    }
                }
            }
            return;
        }

        auto candidates = BattleHighlighter::GetCandidates(
            ctx.playerCol, ctx.playerRow, data->rangeType, data->range);

        for (auto enemy : *ctx.enemies)
        {
            bool inRange = false;

            if (data->rangeType == RangeType::Area)
            {
                for (auto& [dc, dr] : enemy->GetGridShape())
                {
                    int ec = enemy->gridCol + dc;
                    int er = enemy->gridRow + dr;
                    for (auto& [cc, cr] : candidates)
                        if (cc == ec && cr == er) { inRange = true; break; }
                    if (inRange) break;
                }
            }
            else
            {
                for (auto& [dc, dr] : enemy->GetGridShape())
                {
                    if (enemy->gridCol + dc == ctx.hoveredCell.first &&
                        enemy->gridRow + dr == ctx.hoveredCell.second)
                    {
                        for (auto& [dc2, dr2] : enemy->GetGridShape())
                            if (RangeShape::Contains(ctx.playerCol, ctx.playerRow,
                                enemy->gridCol + dc2, enemy->gridRow + dr2, data->rangeType, data->range))
                            {
                                inRange = true; break;
                            }
                        break;
                    }
                }
            }

            if (inRange)
            {
                float sx, sy;
                if (GetEnemyScreenPos(enemy, ctx.renderer3D, sx, sy))
                    DrawArrowIndicator(sx, sy, arrowColor, ctx.highlightTimer);

                // ノックバック/引き寄せプレビュー
                if (data->onHitEffect.hasEffect)
                {
                    if (data->onHitEffect.type == CardEffectType::Knockback)
                    {
                        auto preview = CardExecutor::PreviewKnockback(
                            enemy, ctx.playerCol, ctx.playerRow,
                            data->onHitEffect.value, ctx.gridMap, *ctx.enemies);

                        if (preview.immovable)
                        {
                            // ×マーク（敵は動かない）
                            m_spriteRenderer->DrawSprite(m_whiteTexture,
                                sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, 0.78f,
                                XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));
                            m_spriteRenderer->DrawSprite(m_whiteTexture,
                                sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, -0.78f,
                                XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));
                        }
                        else if (preview.destCol != enemy->gridCol || preview.destRow != enemy->gridRow)
                        {
                            float wx, wz;
                            GridToWorld(ctx.gridMap, preview.destCol, preview.destRow, wx, wz);

                            XMVECTOR worldPos = XMVectorSet(wx - 0.5f, 0.01f, wz - 0.5f, 1.0f);
                            XMMATRIX view = ctx.renderer3D->GetViewMatrix();
                            XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
                            XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
                            XMFLOAT4 clip;
                            XMStoreFloat4(&clip, clipPos);

                            if (clip.w > 0.0f)
                            {
                                float dx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
                                float dy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;

                                ID3D11ShaderResourceView* tex = TextureManager::Get(enemy->GetTextureName());
                                if (tex)
                                {
                                    float ghostW = enemy->width * 50.0f;
                                    float ghostH = enemy->height * 50.0f;
                                    m_spriteRenderer->DrawSprite(tex,
                                        dx - ghostW / 2.0f, dy - ghostH,
                                        ghostW, ghostH, 0.0f,
                                        XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f));
                                }
                                if (preview.hasCollision)
                                {
                                    for (auto* other : *ctx.enemies)
                                    {
                                        if (other->gridCol == preview.collisionCol && other->gridRow == preview.collisionRow)
                                        {
                                            float osx, osy;
                                            if (GetEnemyScreenPos(other, ctx.renderer3D, osx, osy))
                                                DrawArrowIndicator(osx, osy, XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f), ctx.highlightTimer);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    else if (data->onHitEffect.type == CardEffectType::Pull)
                    {
                        auto preview = CardExecutor::PreviewPull(
                            enemy, ctx.playerCol, ctx.playerRow,
                            data->onHitEffect.value, ctx.gridMap, *ctx.enemies);

                        if (preview.immovable)
                        {
                            // ×マーク（敵は動かない）
                            m_spriteRenderer->DrawSprite(m_whiteTexture,
                                sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, 0.78f,
                                XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));
                            m_spriteRenderer->DrawSprite(m_whiteTexture,
                                sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, -0.78f,
                                XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));

                            // プレイヤーの移動先表示
                            if (preview.playerDestCol != ctx.playerCol || preview.playerDestRow != ctx.playerRow)
                            {
                                float wx, wz;
                                GridToWorld(ctx.gridMap, preview.playerDestCol, preview.playerDestRow, wx, wz);
                                XMVECTOR worldPos = XMVectorSet(wx - 0.5f, 0.01f, wz - 0.5f, 1.0f);
                                XMMATRIX view = ctx.renderer3D->GetViewMatrix();
                                XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
                                XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
                                XMFLOAT4 clip;
                                XMStoreFloat4(&clip, clipPos);

                                if (clip.w > 0.0f)
                                {
                                    float px = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
                                    float py = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
                                    ID3D11ShaderResourceView* playerTex = TextureManager::Get("player");
                                    if (playerTex)
                                    {
                                        float ghostW = ctx.player->width * 50.0f;
                                        float ghostH = ctx.player->height * 50.0f;
                                        m_spriteRenderer->DrawSprite(playerTex,
                                            px - ghostW / 2.0f, py - ghostH,
                                            ghostW, ghostH, 0.0f,
                                            XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f));
                                    }
                                }
                            }
                        }
                        else if (preview.destCol != enemy->gridCol || preview.destRow != enemy->gridRow)
                        {
                            float wx, wz;
                            GridToWorld(ctx.gridMap, preview.destCol, preview.destRow, wx, wz);

                            XMVECTOR worldPos = XMVectorSet(wx - 0.5f, 0.01f, wz - 0.5f, 1.0f);
                            XMMATRIX view = ctx.renderer3D->GetViewMatrix();
                            XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
                            XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
                            XMFLOAT4 clip;
                            XMStoreFloat4(&clip, clipPos);

                            if (clip.w > 0.0f)
                            {
                                float dx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
                                float dy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;

                                ID3D11ShaderResourceView* tex = TextureManager::Get(enemy->GetTextureName());
                                if (tex)
                                {
                                    float ghostW = enemy->width * 50.0f;
                                    float ghostH = enemy->height * 50.0f;
                                    m_spriteRenderer->DrawSprite(tex,
                                        dx - ghostW / 2.0f, dy - ghostH,
                                        ghostW, ghostH, 0.0f,
                                        XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if (data->type == CardType::Skill || data->type == CardType::Power)
    {
        XMFLOAT4 arrowColor = (data->type == CardType::Power)
            ? XMFLOAT4(0.8f, 0.3f, 0.9f, 1.0f)   // パワー＝紫
            : XMFLOAT4(0.2f, 0.8f, 1.0f, 1.0f);  // スキル＝青

        float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
        XMVECTOR worldPos = XMVectorSet(
            ctx.player->worldX,
            ctx.player->worldY + ctx.player->height * cos(pitch),
            ctx.player->worldZ + 0.5f - ctx.player->height * sin(pitch),
            1.0f
        );
        XMMATRIX view = ctx.renderer3D->GetViewMatrix();
        XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
        XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, clipPos);

        if (data->mainEffect.type == CardEffectType::PlaceTrap)
        {
            auto& cell = ctx.gridMap->GetCell(ctx.playerCol, ctx.playerRow);
            if (cell.tileEffect.active)
            {
                // プレイヤーの頭上に×マーク
                float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
                XMVECTOR worldPos = XMVectorSet(
                    ctx.player->worldX,
                    ctx.player->worldY + ctx.player->height * cos(pitch),
                    ctx.player->worldZ + 0.5f - ctx.player->height * sin(pitch),
                    1.0f);
                XMMATRIX view = ctx.renderer3D->GetViewMatrix();
                XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
                XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
                XMFLOAT4 clip;
                XMStoreFloat4(&clip, clipPos);
                if (clip.w > 0.0f)
                {
                    float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
                    float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
                    m_spriteRenderer->DrawSprite(m_whiteTexture,
                        sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, 0.78f,
                        XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));
                    m_spriteRenderer->DrawSprite(m_whiteTexture,
                        sx - 12.0f, sy - 62.0f, 24.0f, 4.0f, -0.78f,
                        XMFLOAT4(1.0f, 0.2f, 0.2f, 0.9f));
                }
            }
        }

        if (clip.w > 0.0f)
        {
            float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
            float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
            DrawArrowIndicator(sx, sy, arrowColor, ctx.highlightTimer);
        }
    }
    else if (data->type == CardType::Move)
    {
        if (!ctx.travelPath || ctx.travelPath->empty()) return;

        auto endCell = ctx.travelPath->back();

        XMFLOAT4 arrowColor(0.2f, 0.9f, 0.3f, 1.0f);

        float wx, wz;
        GridToWorld(ctx.gridMap, endCell.first, endCell.second, wx, wz);

        XMVECTOR worldPos = XMVectorSet(wx - 0.5f, 0.5f, wz - 0.5f, 1.0f);
        XMMATRIX view = ctx.renderer3D->GetViewMatrix();
        XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
        XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, clipPos);

        if (clip.w > 0.0f)
        {
            float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
            float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
            DrawArrowIndicator(sx, sy, arrowColor, ctx.highlightTimer);
        }
    }
}

void BattleUI::DrawArrowIndicator(float sx, float sy, const XMFLOAT4& color, float highlightTimer)
{

    /*float cardAreaY = m_screenHeight - CARD_HEIGHT - CARD_HIDE_Y_OFFSET;
    if (sy > cardAreaY)
        return;*/

    float bob = sin(highlightTimer * 3.0f) * 6.0f;
    float ay = sy - 40.0f + bob;

    XMFLOAT4 outline(0.0f, 0.0f, 0.0f, 1.0f);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 3.0f, ay - 1.0f, 6.0f, 16.0f, 0.0f, outline);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 8.0f, ay + 14.0f, 16.0f, 6.0f, 0.0f, outline);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 5.0f, ay + 19.0f, 10.0f, 5.0f, 0.0f, outline);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 3.0f, ay + 23.0f, 6.0f, 4.0f, 0.0f, outline);

    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 2.0f, ay, 4.0f, 14.0f, 0.0f, color);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 7.0f, ay + 15.0f, 14.0f, 4.0f, 0.0f, color);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 4.0f, ay + 19.0f, 8.0f, 4.0f, 0.0f, color);
    m_spriteRenderer->DrawSprite(m_whiteTexture, sx - 2.0f, ay + 23.0f, 4.0f, 3.0f, 0.0f, color);
}

void BattleUI::DrawPileViewer(const BattleUIContext& ctx)
{
    m_textRenderer->Begin();

    const auto& pile = ctx.showDrawPile
        ? ctx.deck->GetDrawPile()
        : ctx.showDiscardPile
        ? ctx.deck->GetDiscardPile()
        : ctx.deck->GetExhaustPile();

    const wchar_t* title;
    if (ctx.cardSelecting)
        title = ctx.showDrawPile ? L"山札から選択" : L"捨て札から選択";
    else
        title = ctx.showDrawPile ? L"山札"
        : ctx.showDiscardPile ? L"捨て札"
        : L"廃棄札";

    float bgX = m_screenWidth / 2.0f - 300.0f;
    float bgY = 50.0f;
    float bgW = 600.0f;
    float bgH = 580.0f;

    m_spriteRenderer->Begin();
    DrawWindow(bgX, bgY, bgW, bgH);
    m_spriteRenderer->End();

    m_textRenderer->DrawText(title, bgX + 20.0f, bgY + 10.0f, 24.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    if (ctx.cardSelecting)
        m_textRenderer->DrawText(L"[カードをクリックして選択]", bgX + bgW - 220.0f, bgY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));
    else
        m_textRenderer->DrawText(L"[枠外クリックで閉じる]", bgX + bgW - 150.0f, bgY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::Gray));

    std::vector<std::string> displayPile = pile;

    if (ctx.showDrawPile)
        std::sort(displayPile.begin(), displayPile.end());

    const float scale = 0.7f;
    const float cw = CardVisual::CARD_W * scale;
    const float ch = CardVisual::CARD_H * scale;
    const float gap = 12.0f;
    const int   cols = 6;

    // 本体（スプライト）
    m_textRenderer->End();
    m_spriteRenderer->Begin();
    for (int i = 0; i < (int)displayPile.size(); i++)
    {
        const CardData* data = CardDataBase::Get(displayPile[i]);
        if (!data) continue;

        float bx = bgX + 25.0f + (i % cols) * (cw + gap);
        float by = bgY + 50.0f + (i / cols) * (ch + gap);

        XMFLOAT4 col = CardVisual::GetCardColor(data->type, false);
        if (ctx.cardSelecting)
        {
            float rx, ry, rw, rh; CardVisual::GetRect(bx, by, scale, rx, ry, rw, rh);
            if (ctx.mousePos.x >= rx && ctx.mousePos.x <= rx + rw
                && ctx.mousePos.y >= ry && ctx.mousePos.y <= ry + rh)
                col = XMFLOAT4(min(1.0f, col.x + 0.3f), min(1.0f, col.y + 0.3f),
                    min(1.0f, col.z + 0.3f), 1.0f);
        }

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, bx, by, scale, 0.0f,
            col, data, 0.0f);
    }
    m_spriteRenderer->End();

    // 文字（テキスト）
    m_textRenderer->Begin();
    for (int i = 0; i < (int)displayPile.size(); i++)
    {
        const CardData* data = CardDataBase::Get(displayPile[i]);
        if (!data) continue;

        float bx = bgX + 25.0f + (i % cols) * (cw + gap);
        float by = bgY + 50.0f + (i / cols) * (ch + gap);
        CardVisual::DrawTexts(m_textRenderer, data, ctx.player, bx, by, scale, 0.0f, 1.0f);
    }

    m_textRenderer->End();
}

void BattleUI::StartDrawCardEffect(const std::string& cardId)
{
    Audio::PlaySE("Assets/Sound/se/draw.mp3");

    DrawCardEffect effect;
    effect.cardId = cardId;
    effect.x = 20.0f;
    effect.y = m_screenHeight - 60.0f;
    effect.targetX = m_screenWidth / 2.0f;
    effect.targetY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    m_drawCardEffects.push_back(effect);
}

void BattleUI::UpdateDrawCardEffects(float deltaTime)
{
    for (auto& effect : m_drawCardEffects)
    {
        if (effect.done) continue;

        effect.timer += deltaTime;
        float t = min(1.0f, effect.timer / DRAW_EFFECT_DURATION);

        float ease = 1.0f - (1.0f - t) * (1.0f - t);

        effect.x = 20.0f + (effect.targetX - 20.0f) * ease;
        effect.y = m_screenHeight - 60.0f + (effect.targetY - (m_screenHeight - 60.0f)) * ease;
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
        const CardData* d = CardDataBase::Get(effect.cardId);
        XMFLOAT4 color = d ? CardVisual::GetCardColor(d->type, false)
            : XMFLOAT4(0.2f, 0.4f, 0.8f, 1.0f);
        color.w = effect.alpha;

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
            effect.x, effect.y, 1.0f, 0.0f, color, d, 0.0f);
    }
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
    effect.startX = m_screenWidth / 2.0f - CARD_WIDTH / 2.0f;   // 手札の位置から
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
        e.timer += deltaTime;
        float t = min(1.0f, e.timer / DISCARD_EFFECT_DUR);
        e.alpha = 1.0f - t;
        if (t >= 1.0f) e.done = true;
    }
    m_discardCardEffects.erase(
        std::remove_if(m_discardCardEffects.begin(), m_discardCardEffects.end(),
            [](const DiscardCardEffect& e) { return e.done; }),
        m_discardCardEffects.end());
}

void BattleUI::DrawDiscardEffects()
{
    float targetX = 80.0f;
    float targetY = (float)(m_screenHeight - 60);

    for (auto& e : m_discardCardEffects)
    {
        float t = min(1.0f, e.timer / DISCARD_EFFECT_DUR);
        float ease = t * t;
        float x = e.startX + (targetX - e.startX) * ease;
        float y = e.startY + (targetY - e.startY) * ease;

        y -= sinf(t * 3.14159f) * 70.0f;      // 途中でふわっと上へ（山なり）

        XMFLOAT4 color(0.4f, 0.4f, 0.4f, e.alpha);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y,
            CARD_WIDTH, CARD_HEIGHT, 0.0f, color);
    }
}

void BattleUI::UpdatePlayCardEffects(float deltaTime)
{  
    for (auto& e : m_playCardEffects)
    {
        if (e.done) continue;
        e.timer += deltaTime;
        float t = min(1.0f, e.timer / PLAY_EFFECT_DUR);
        e.alpha = (t < 0.4f) ? 1.0f : 1.0f - (t - 0.4f) / 0.6f;   // 後半でフェード
        if (t >= 1.0f) e.done = true;
    }
    m_playCardEffects.erase(
        std::remove_if(m_playCardEffects.begin(), m_playCardEffects.end(),
            [](const PlayCardEffect& e) { return e.done; }),
        m_playCardEffects.end());
}

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
        anim.currentX = 20.0f;
        anim.currentY = (float)(m_screenHeight - 60);
        m_cardAnims.push_back(anim);
    }
    while ((int)m_cardAnims.size() > handSize)
        m_cardAnims.pop_back();

    float cardHideY = m_screenHeight - CARD_HIDE_Y_OFFSET;
    float cardHoverY = m_screenHeight - CARD_HEIGHT - CARD_HOVER_Y_OFFSET;
    float speed = 12.0f;
    float dt = min(deltaTime, 0.03f);

    for (int i = 0; i < handSize; i++)
    {
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
            if (selectedNeedsTarget)
            {
                if (mousePos.y < m_screenHeight - 100)
                    m_cardLockedToCenter = true;

                if (m_cardLockedToCenter)
                {
                    targetX = m_screenWidth / 2.0f - CARD_WIDTH / 2.0f;
                    targetY = cardHoverY + 40.0f;
                }
                else
                {
                    targetX = (float)mousePos.x - CARD_WIDTH / 2.0f;
                    targetY = (float)mousePos.y - CARD_HEIGHT / 2.0f;
                }
            }
            else
            {
                targetX = (float)mousePos.x - CARD_WIDTH / 2.0f;
                targetY = (float)mousePos.y - CARD_HEIGHT / 2.0f;
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

int BattleUI::GetCardAtScreenPos(POINT p) const
{
    int n = (int)m_cardAnims.size();
    if (n == 0) return -1;

    // 縦は余裕を持たせる（下は画面外まで）
    float topY = m_screenHeight - CARD_HIDE_Y_OFFSET - 40.0f;
    // 上に描かれている（右・ホバー中）カードを優先 → 逆順で最初のヒット
    for (int i = n - 1; i >= 0; i--)
    {
        float x, y, w, h;
        CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, x, y, w, h);   // 実際の描画位置・拡大に一致
        if (p.x >= x && p.x <= x + w && p.y >= topY)
            return i;
    }
    return -1;
}


void BattleUI::DrawPlayerOffScreenIndicator(const BattleUIContext& ctx)
{
    float pitch = XMConvertToRadians(-Renderer3D::BILLBOARD_PITCH);
    XMVECTOR worldPos = XMVectorSet(
        ctx.player->worldX,
        ctx.player->worldY + ctx.player->height * cos(pitch),
        ctx.player->worldZ + 0.5f - ctx.player->height * sin(pitch),
        1.0f
    );
    XMMATRIX view = ctx.renderer3D->GetViewMatrix();
    XMMATRIX proj = ctx.renderer3D->GetProjectionMatrix();
    XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, clipPos);

    if (clip.w <= 0.0f) return;

    float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;

    float margin = 40.0f;

    // 画面内にいたら何もしない
    if (sx >= margin && sx <= m_screenWidth - margin
        && sy >= margin && sy <= m_screenHeight - margin)
        return;

    // 画面端にクランプ
    float edgeX = max(margin, min((float)m_screenWidth - margin, sx));
    float edgeY = max(margin, min((float)m_screenHeight - margin, sy));

    // プレイヤーアイコン（縮小表示）
    float iconSize = 32.0f;
    ID3D11ShaderResourceView* playerTex = TextureManager::Get("player");
    m_spriteRenderer->DrawSprite(playerTex,
        edgeX - iconSize / 2.0f, edgeY - iconSize / 2.0f,
        iconSize, iconSize, 0.0f,
        XMFLOAT4(1.0f, 1.0f, 1.0f, 0.8f));

    // 矢印（プレイヤー方向を示す三角形）
    float dx = sx - edgeX;
    float dy = sy - edgeY;
    float len = sqrtf(dx * dx + dy * dy);
    if (len > 0.0f)
    {
        dx /= len;
        dy /= len;
        float arrowX = edgeX + dx * 20.0f;
        float arrowY = edgeY + dy * 20.0f;
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            arrowX - 4.0f, arrowY - 4.0f,
            8.0f, 8.0f, 0.0f,
            XMFLOAT4(1.0f, 1.0f, 0.0f, 0.9f));
    }
}

void BattleUI::DrawEnemyInfoPanel(const BattleUIContext& ctx)
{
    float panelX = ctx.screenWidth - 250.0f;
    float panelY = 50.0f;
    float panelW = 240.0f;
    float entryH = 90.0f;
    float iconSize = 55.0f;

    POINT mp = ctx.mousePos;
    int hoveredEnemy = -1;

    for (int i = 0; i < (int)ctx.enemies->size(); i++)
    {
        Enemy* enemy = (*ctx.enemies)[i];
        float entryY = panelY + i * (entryH + 5.0f);

        // 背景
        float detailX = panelX - 200.0f;
        // 詳細パネルの高さを計算
        float detailH = 10.0f;
        detailH += 22.0f * (float)enemy->GetPlannedActions().size();
        if (enemy->GetBlock() > 0) detailH += 18.0f;
        if (enemy->IsImmovable()) detailH += 20.0f;
        for (auto& buff : enemy->GetBuffManager().GetBuffs())
            detailH += 20.0f;
        detailH += 18.0f;
        if (detailH < entryH) detailH = entryH;
        bool isEntryHover = (mp.x >= panelX && mp.x <= panelX + panelW
            && mp.y >= entryY && mp.y <= entryY + entryH);
        bool isDetailHover = (m_panelHoveredEnemy == i)
            && (mp.x >= detailX && mp.x <= panelX
                && mp.y >= entryY && mp.y <= entryY + detailH);
        bool isHover = isEntryHover || isDetailHover;

        bool isSel = (ctx.selectedEnemy == i);
        DrawWindow(panelX, entryY, panelW, entryH);
        if (isHover || isSel)   // 選択/ホバーは水色を薄く重ねて強調
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX + 5.0f, entryY + 5.0f,
                panelW - 10.0f, entryH - 10.0f, 0.0f, XMFLOAT4(0.2f, 0.5f, 0.7f, 0.30f));

        if (isHover || isSel)
        {
            XMFLOAT4 line(0.2f, 0.7f, 1.0f, 1.0f);
            const float th = 3.0f, L = 14.0f;
            float rx = panelX + panelW, by = entryY + entryH;
            // 左上
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, entryY, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, entryY, th, L, 0.0f, line);
            // 右上
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - L, entryY, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - th, entryY, th, L, 0.0f, line);
            // 左下
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, by - th, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, by - L, th, L, 0.0f, line);
            // 右下
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - L, by - th, L, th, 0.0f, line);
            m_spriteRenderer->DrawSprite(m_whiteTexture, rx - th, by - L, th, L, 0.0f, line);
        }

        // アイコン
        ID3D11ShaderResourceView* tex = TextureManager::Get(enemy->GetTextureName());
        if (tex)
        {
            m_spriteRenderer->DrawSprite(tex, panelX + 5.0f, entryY + 5.0f,
                iconSize, iconSize, 0.0f, XMFLOAT4(1, 1, 1, 1));
        }

        // HPバー
        float barX = panelX + iconSize + 10.0f;
        float barY = entryY + 5.0f;
        float barW = panelW - iconSize - 20.0f;
        float barH = 12.0f;

        HPBarInfo panelBar;
        panelBar.currentHP = enemy->GetHp();
        panelBar.maxHP = enemy->GetMaxHp();
        panelBar.displayHP = enemy->GetDisplayHp();
        panelBar.block = enemy->GetBlock();
        int tr = 1 + ctx.player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
        int pp = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
        int psum = 0;
        for (int i = 0; i < tr; i++) { int v = pp - i; if (v <= 0) break; psum += v; }
        panelBar.poisonDmg = psum;
        panelBar.hasBurn = enemy->GetBuffManager().HasBuff(BuffType::Burn);
        DrawHPBar(barX, barY, barW, barH, panelBar, ctx.highlightTimer);

        if (enemy->GetBlock() > 0)
        {
            float bIconSize = barH * 1.5f;
            float bIconX = barX - bIconSize * 0.35f;
            float bIconY = barY + (barH - bIconSize) / 2.0f;
            m_spriteRenderer->DrawSprite(m_whiteTexture,
                bIconX - 1.0f, bIconY - 1.0f, bIconSize + 2.0f, bIconSize + 2.0f,
                0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
            m_spriteRenderer->DrawSprite(m_whiteTexture,
                bIconX, bIconY, bIconSize, bIconSize,
                0.0f, XMFLOAT4(0.3f, 0.6f, 1.0f, 1.0f));
        }

        if (isHover && hoveredEnemy < 0)
            hoveredEnemy = i;

    }

    // ホバー中の詳細背景（スプライト）
    if (hoveredEnemy >= 0)
    {
        Enemy* hEnemy = (*ctx.enemies)[hoveredEnemy];
        float detailX = panelX - 200.0f;
        float detailEntryY = panelY + hoveredEnemy * (entryH + 5.0f);
        float detailH = 10.0f;
        detailH += 22.0f * (float)hEnemy->GetPlannedActions().size();
        if (hEnemy->GetBlock() > 0) detailH += 18.0f;
        if (hEnemy->IsImmovable()) detailH += 20.0f;
        for (auto& buff : hEnemy->GetBuffManager().GetBuffs())
            detailH += 20.0f;
        detailH += 18.0f;
        if (detailH < entryH) detailH = entryH;
        DrawWindow(detailX, detailEntryY, 190.0f, detailH);
    }

    m_panelHoveredEnemy = hoveredEnemy;

    m_spriteRenderer->End();
    m_textRenderer->Begin();

    // テキスト描画（全エネミーまとめて）
    for (int i = 0; i < (int)ctx.enemies->size(); i++)
    {
        Enemy* enemy = (*ctx.enemies)[i];
        float entryY = panelY + i * (entryH + 5.0f);

        // HP数値
        wchar_t hpText[32];
        swprintf_s(hpText, L"%d/%d", enemy->GetHp(), enemy->GetMaxHp());
        m_textRenderer->DrawText(hpText,
            panelX + iconSize + 10.0f, entryY + 16.0f, 15.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        if (enemy->GetBlock() > 0)
        {
            float barX = panelX + iconSize + 10.0f;
            float barY = entryY + 5.0f;
            float barH = 12.0f;
            float bIconSize = barH * 1.5f;
            float bIconX = barX - bIconSize * 0.35f;
            float bIconY = barY + (barH - bIconSize) / 2.0f;
            wchar_t blockText[16];
            swprintf_s(blockText, L"%d", enemy->GetBlock());
            float bFont = 11.0f;
            float bTextW = wcslen(blockText) * bFont * 0.5f;
            m_textRenderer->DrawText(blockText,
                bIconX + (bIconSize - bTextW) / 2.0f + 1.0f,
                bIconY + (bIconSize - bFont) / 2.0f + 1.0f,
                bFont, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
            m_textRenderer->DrawText(blockText,
                bIconX + (bIconSize - bTextW) / 2.0f,
                bIconY + (bIconSize - bFont) / 2.0f,
                bFont, D2D1::ColorF(D2D1::ColorF::White));
        }

        // 次の行動
        const EnemyAction* action = enemy->GetNextAction();
        if (action)
        {
            std::wstring dispText =
                EnemyIntentVisual::GetActionText(*action, enemy->GetBuffManager());

            m_textRenderer->DrawText(dispText.c_str(),
                panelX + iconSize + 10.0f, entryY + 32.0f, 14.0f,
                D2D1::ColorF(D2D1::ColorF::Orange));
        }

        // ホバー中の詳細テキスト
        if (hoveredEnemy == i)
        {
            float detailX = panelX - 200.0f;
            float lineY = entryY + 5.0f;

            for (auto& act : enemy->GetPlannedActions())
            {
                std::wstring actionText =
                    EnemyIntentVisual::GetActionText(act, enemy->GetBuffManager());
                m_textRenderer->DrawText(actionText.c_str(),
                    detailX + 10.0f, lineY, 15.0f, D2D1::ColorF(1.0f, 0.8f, 0.3f));
                lineY += 22.0f;
            }

            if (enemy->GetBlock() > 0)
            {
                wchar_t blockText[32];
                swprintf_s(blockText, L"Block: %d", enemy->GetBlock());
                m_textRenderer->DrawText(blockText,
                    detailX + 10.0f, lineY, 15.0f,
                    D2D1::ColorF(D2D1::ColorF::LightBlue));
                lineY += 18.0f;
            }

            // 移動不可表示
            if (enemy->IsImmovable())
            {
                m_textRenderer->DrawText(L"移動不可",
                    detailX + 10.0f, lineY, 14.0f,
                    D2D1::ColorF(0.9f, 0.4f, 0.4f));
                lineY += 20.0f;
            }

            for (auto& buff : enemy->GetBuffManager().GetBuffs())
            {
                const auto& info = BuffInfo::Get(buff.type);
                std::wstring buffText;
                if (BuffInfo::IsDurationBased(buff.type))
                    buffText = info.name + L": " + std::to_wstring(buff.duration) + L"ターン";
                else
                {
                    buffText = info.name + L": " + std::to_wstring(buff.value);
                    if (buff.type != BuffType::Poison)
                        buffText += L" (" + std::to_wstring(buff.duration) + L"T)";
                }
                bool buffHover = (mp.x >= detailX && mp.x <= detailX + 190.0f
                    && mp.y >= lineY && mp.y <= lineY + 20.0f);
                D2D1::ColorF buffColor = buffHover
                    ? D2D1::ColorF(1.0f, 1.0f, 0.5f)
                    : D2D1::ColorF(0.6f, 1.0f, 0.6f);
                m_textRenderer->DrawText(buffText.c_str(),
                    detailX + 10.0f, lineY, 14.0f, buffColor);
                lineY += 20.0f;
                if (buffHover)
                {
                    int descNum = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
                    std::wstring desc = BuffInfo::GetDescription(buff.type, descNum);
                    m_textRenderer->DrawText(desc.c_str(),
                        detailX + 10.0f, lineY, 12.0f,
                        D2D1::ColorF(0.8f, 0.8f, 0.8f));
                    lineY += 18.0f;
                }
            }
        }
    }
    m_textRenderer->End();
}

void BattleUI::DrawWindow(float x, float y, float w, float h, const XMFLOAT4& tint)
{
    UiWindow::Draw(m_spriteRenderer, m_whiteTexture, x, y, w, h, tint);
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

void BattleUI::DrawDropTooltipTop()
{
    if (m_hoverDropId.empty()) return;
    ItemTooltip::Draw(m_spriteRenderer, m_textRenderer, m_whiteTexture,
        m_hoverDropId, m_hoverDropX, m_hoverDropY, m_screenWidth, m_screenHeight, true);
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
        effect.startX = m_screenWidth / 2.0f - CARD_WIDTH / 2.0f;
        effect.startY = (float)m_screenHeight - CARD_HEIGHT;
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
    else { effect.startX = m_screenWidth / 2.0f; effect.startY = (float)m_screenHeight - CARD_HEIGHT; }
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    effect.data = data;
    effect.cardType = data ? data->type : CardType::Skill;
    m_playCardEffects.push_back(effect);
}

void BattleUI::GetPlayEffectTransform(const PlayCardEffect& e, float& x, float& y, float& scale)
{
    float t = min(1.0f, e.timer / PLAY_EFFECT_DUR);
    scale = (t < 0.6f)
        ? 1.0f + 0.4f * (t / 0.6f)                  // 中央へ向かいながら拡大
        : 1.4f - 1.1f * ((t - 0.6f) / 0.4f);        // 着いてから一気に縮む
    float ease = 1.0f - (1.0f - t) * (1.0f - t);
    float tx = m_screenWidth / 2.0f - CARD_WIDTH / 2.0f;
    float ty = m_screenHeight / 2.0f - CARD_HEIGHT / 2.0f;
    float bx = e.startX + (tx - e.startX) * ease;
    float by = e.startY + (ty - e.startY) * ease;
    float w = CARD_WIDTH * scale, h = CARD_HEIGHT * scale;
    x = bx - (w - CARD_WIDTH) / 2.0f;
    y = by - (h - CARD_HEIGHT) / 2.0f;
}

void BattleUI::DrawPlayCardEffects()
{
    for (auto& e : m_playCardEffects)
    {
        float x, y, s;
        GetPlayEffectTransform(e, x, y, s);
        // GetPlayEffectTransform は左上座標なので、DrawBase の基準座標に戻す
        float baseX = x + (CardVisual::CARD_W * s - CardVisual::CARD_W) / 2.0f;
        float baseY = y + (CardVisual::CARD_H * s - CardVisual::CARD_H) / 2.0f;

        XMFLOAT4 color = CardVisual::GetCardColor(e.cardType, false);
        color.w = e.alpha;

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture,
            baseX, baseY, s, 0.0f, color, e.data, 0.0f);
    }
}

void BattleUI::DrawPlayCardEffectTexts(const BattleUIContext& ctx)
{
    for (auto& e : m_playCardEffects)
    {
        if (!e.data) continue;
        float x, y, s;
        GetPlayEffectTransform(e, x, y, s);
        // GetPlayEffectTransform は左上座標を返すので、中心基準に戻す
        float baseX = x + (CardVisual::CARD_W * s - CardVisual::CARD_W) / 2.0f;
        float baseY = y + (CardVisual::CARD_H * s - CardVisual::CARD_H) / 2.0f;

        CardVisual::DrawTexts(m_textRenderer, e.data, ctx.player,
            baseX, baseY, s, 0.0f, e.alpha);
    }
}

bool BattleUI::WorldToScreen(float wx, float wy, float wz, Renderer3D* renderer3D,
    float& outX, float& outY) const
{
    XMMATRIX view = renderer3D->GetViewMatrix();
    XMMATRIX proj = renderer3D->GetProjectionMatrix();
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, XMVector4Transform(XMVectorSet(wx, wy, wz, 1.0f), view * proj));
    if (clip.w <= 0.0f) return false;
    outX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    outY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
}

void BattleUI::DrawFloatingTexts(const BattleUIContext& ctx)
{
    for (auto& t : FloatingTextManager::GetAll())
    {
        float rise, alpha, size;
        FloatingTextManager::GetVisual(t, rise, alpha, size);

        float sx, sy;
        if (!WorldToScreen(t.worldX, t.worldY + rise, t.worldZ, ctx.renderer3D, sx, sy))
            continue;

        sx += t.offsetX - t.text.size() * size * 0.25f;    // 中央寄せ

        float o = size * 0.06f;                        // 文字サイズに比例させる
        const float dir[4][2] = { {-1,0},{1,0},{0,-1},{0,1} };
        for (auto& d : dir)
            m_textRenderer->DrawText(t.text.c_str(), sx + d[0] * o, sy + d[1] * o, size,
                D2D1::ColorF(0, 0, 0, alpha));
        m_textRenderer->DrawText(t.text.c_str(), sx, sy, size,
            D2D1::ColorF(t.color.x, t.color.y, t.color.z, alpha));
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

void BattleUI::StartDiscardEffectAt(int cardIndex)
{
    if (cardIndex < 0 || cardIndex >= (int)m_cardAnims.size()) return;

    DiscardCardEffect effect;
    effect.startX = m_cardAnims[cardIndex].currentX;   // そのカードの今の位置から
    effect.startY = m_cardAnims[cardIndex].currentY;
    effect.alpha = 1.0f;
    effect.timer = 0.0f;
    effect.done = false;
    m_discardCardEffects.push_back(effect);
}

void BattleUI::DrawUnitStatusSprites(float footX, float footY, float scale, bool isBoss,
    const HPBarInfo& bar, BuffManager& bm, POINT mousePos, float timer)
{
    float barWidth = (isBoss ? 100.0f : 50.0f) * scale;
    float barHeight = (isBoss ? 15.0f : 10.0f) * scale;
    float barX = footX - barWidth / 2.0f;
    float barY = footY - 30.0f;

    DrawHPBar(barX, barY, barWidth, barHeight, bar, timer);

    // ブロックアイコン（HPバー左端）
    if (bar.block > 0)
    {
        float iconSize = barHeight * 1.5f;
        float iconX = barX - iconSize / 2.0f;
        float iconY = barY + (barHeight - iconSize) / 2.0f;
        m_spriteRenderer->DrawSprite(m_whiteTexture, iconX - 1.0f, iconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f, 0.0f, XMFLOAT4(0, 0, 0, 1));
        m_spriteRenderer->DrawSprite(m_whiteTexture, iconX, iconY, iconSize, iconSize, 0.0f, XMFLOAT4(0.3f, 0.6f, 1.0f, 1.0f));
    }

    // バフ/デバフアイコン
    float bIconY = barY + barHeight + 4.0f, bIconX = barX;
    float bMaxX = barX + barWidth + 40.0f;
    for (auto& buff : bm.GetBuffs())
    {
        float iconSize = 16.0f;
        if (bIconX + iconSize + 20.0f > bMaxX) { bIconX = barX; bIconY += iconSize + 6.0f; }
        XMFLOAT4 buffColor = BuffInfo::Get(buff.type).color;
        bool hov = (mousePos.x >= bIconX && mousePos.x <= bIconX + iconSize
            && mousePos.y >= bIconY && mousePos.y <= bIconY + iconSize);
        if (hov)
        {
            m_hasHoveredBuff = true; m_hoveredBuffType = buff.type;
            m_hoveredBuffValue = BuffInfo::IsDurationBased(buff.type) ? buff.duration : buff.value;
            m_hoveredBuffX = bIconX; m_hoveredBuffY = bIconY;
        }
        auto btex = TextureManager::Get(BuffInfo::Get(buff.type).texture);
        if (btex)
            m_spriteRenderer->DrawSprite(btex, bIconX, bIconY, iconSize, iconSize, 0.0f, hov ? XMFLOAT4(1, 1, 1, 1) : XMFLOAT4(0.85f, 0.85f, 0.85f, 1));
        else
        {
            m_spriteRenderer->DrawSprite(m_whiteTexture, bIconX - 1, bIconY - 1, iconSize + 2, iconSize + 2, 0.0f, XMFLOAT4(0, 0, 0, 1));
            m_spriteRenderer->DrawSprite(m_whiteTexture, bIconX, bIconY, iconSize, iconSize, 0.0f, hov ? XMFLOAT4(1, 1, 1, 1) : buffColor);
        }
        bIconX += iconSize + 20.0f;
    }
}

void BattleUI::DrawUnitStatusText(float footX, float footY, float scale, bool isBoss,
    const HPBarInfo& bar, BuffManager& bm)
{
    float barWidth = (isBoss ? 100.0f : 50.0f) * scale;
    float barHeight = (isBoss ? 15.0f : 10.0f) * scale;
    float barX = footX - barWidth / 2.0f;
    float barY = footY - 30.0f;
    float fontSize = max(8.0f, (isBoss ? 10.0f : 8.0f) * scale);

    // HP数値
    wchar_t hp[32]; swprintf_s(hp, L"%d/%d", bar.currentHP, bar.maxHP);
    float tw = wcslen(hp) * fontSize * 0.5f;
    float tx = barX + (barWidth - tw) / 2.0f;
    float ty = barY + (barHeight - fontSize) / 2.0f - 3.0f;
    m_textRenderer->DrawText(hp, tx + 1, ty + 1, fontSize, D2D1::ColorF(0, 0, 0, 1));
    m_textRenderer->DrawText(hp, tx, ty, fontSize, D2D1::ColorF(D2D1::ColorF::White));

    // ブロック数値
    if (bar.block > 0)
    {
        float iconSize = barHeight * 1.5f;
        float iconX = barX - iconSize / 2.0f;
        float iconY = barY + (barHeight - iconSize) / 2.0f - 2.0f;
        wchar_t bt[16]; swprintf_s(bt, L"%d", bar.block);
        float bFont = max(7.0f, fontSize * 0.9f);
        float btW = wcslen(bt) * bFont * 0.5f;
        m_textRenderer->DrawText(bt, iconX + (iconSize - btW) / 2.0f + 1.0f, iconY + (iconSize - bFont) / 2.0f + 1.0f, bFont, D2D1::ColorF(0, 0, 0, 1));
        m_textRenderer->DrawText(bt, iconX + (iconSize - btW) / 2.0f, iconY + (iconSize - bFont) / 2.0f, bFont, D2D1::ColorF(D2D1::ColorF::White));
    }

    // バフ/デバフ数値
    float vY = barY + barHeight + 4.0f, vX = barX, vMaxX = barX + barWidth + 40.0f;
    for (auto& buff : bm.GetBuffs())
    {
        float iconSize = 16.0f;
        if (vX + iconSize + 20.0f > vMaxX) { vX = barX; vY += iconSize + 6.0f; }
        wchar_t bv[16];
        if (BuffInfo::IsDurationBased(buff.type)) swprintf_s(bv, L"%dT", buff.duration);
        else swprintf_s(bv, L"%d", buff.value);
        m_textRenderer->DrawText(bv, vX + iconSize + 2.0f, vY + 1.0f, 11.0f, D2D1::ColorF(D2D1::ColorF::White));
        vX += iconSize + 20.0f;
    }
}

bool BattleUI::GetHitmarkRect(Enemy* enemy, Renderer3D* r3d, float& x, float& y, float& w, float& h) const
{
    float hx, hy;
    if (!GetEnemyScreenPos(enemy, r3d, hx, hy)) return false;
    const float mk = 34.0f;                       // 当たりマーカーと同じレイアウト
    x = hx - mk / 2.0f - 8.0f;
    y = hy - mk - 14.0f - 8.0f;
    w = mk + 16.0f; h = mk + 16.0f;
    return true;
}