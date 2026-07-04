#include "BattleUI.h"
#include "BuffInfo.h"
#include "CardExecutor.h"
#include <algorithm>

using namespace DirectX;

BattleUI::~BattleUI()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
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
        XMFLOAT4 glowColor = XMFLOAT4(0.3f, 0.5f , 1.0f, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w * hpRatio, h,
            0.0f, glowColor);
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
    XMVECTOR worldPos = XMVectorSet(
        enemy->worldX,
        enemy->worldY + enemy->height * cos(pitch),
        enemy->worldZ + 0.5f - enemy->height * sin(pitch),
        1.0f
    );
    XMMATRIX view = renderer3D->GetViewMatrix();
    XMMATRIX proj = renderer3D->GetProjectionMatrix();
    XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, clipPos);

    if (clip.w <= 0.0f) return false;

    outX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    outY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
}

bool BattleUI::GetEnemyFootPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const
{
    XMVECTOR worldPos = XMVectorSet(
        enemy->worldX,
        0.0f,
        enemy->worldZ + 0.5f,
        1.0f
    );
    XMMATRIX view = renderer3D->GetViewMatrix();
    XMMATRIX proj = renderer3D->GetProjectionMatrix();
    XMVECTOR clipPos = XMVector4Transform(worldPos, view * proj);
    XMFLOAT4 clip;
    XMStoreFloat4(&clip, clipPos);

    if (clip.w <= 0.0f) return false;

    outX = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
    outY = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
    return true;
}

std::wstring BattleUI::GetCardEffectText(const CardData* data, Player* player) const
{
    if (!data) return L"";

    int actualValue = data->mainEffect.value;

    if (data->type == CardType::Attack)
        actualValue = player->GetBuffManager().GetFinalAttack(data->mainEffect.value);
    else if (data->mainEffect.type == CardEffectType::Block)
        actualValue = player->GetBuffManager().GetFinalBlock(data->mainEffect.value);

    std::wstring result = data->description;
    std::wstring placeholder = L"{value}";
    size_t pos = result.find(placeholder);
    if (pos != std::wstring::npos)
        result.replace(pos, placeholder.size(), std::to_wstring(actualValue));

    if (data->exhaust)
        result += L" [消滅]";

    return result;
}

bool BattleUI::IsCardBoosted(const CardData* data, Player* player) const
{
    if (!data) return false;
    if (data->type == CardType::Attack)
        return player->GetBuffManager().HasBuff(BuffType::AttackUp);
    if (data->type == CardType::Skill)
        return player->GetBuffManager().HasBuff(BuffType::DefenseUp);
    return false;
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
    DrawHPBar(20.0f, 60.0f, 200.0f, 30.0f, playerBar, ctx.highlightTimer);
    DrawEnemyGridHighlight(ctx);

    if (ctx.player->GetBlock() > 0)
    {
        float pIconSize = 30.0f * 1.5f;
        float pIconX = 20.0f - pIconSize * 0.35f;
        float pIconY = 60.0f + (30.0f - pIconSize) / 2.0f;
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            pIconX - 1.0f, pIconY - 1.0f, pIconSize + 2.0f, pIconSize + 2.0f,
            0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            pIconX, pIconY, pIconSize, pIconSize,
            0.0f, XMFLOAT4(0.3f, 0.6f, 1.0f, 1.0f));
    }

    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i >= (int)m_cardAnims.size()) continue;
        if (i == ctx.hoveredCardIndex) continue;
        if (i == ctx.selectedCardIndex) continue;

        float cardX = m_cardAnims[i].currentX;

        float drawY = m_cardAnims[i].currentY;

        XMFLOAT4 color = CardVisual::GetCardColor(
            cards[i]->GetData()->type, false);

        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, drawY,
            CARD_WIDTH, CARD_HEIGHT, 0.0f, color);
    }

    if (ctx.hoveredCardIndex >= 0 && ctx.hoveredCardIndex < (int)cards.size())
    {
        int i = ctx.hoveredCardIndex;
        if (i >= (int)m_cardAnims.size()) return;  // もしくは処理をスキップ
        float cardX = m_cardAnims[i].currentX;

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = m_cardAnims[i].currentY;

        XMFLOAT4 color = (i == ctx.selectedCardIndex)
            ? XMFLOAT4(0.8f, 0.8f, 0.0f, 0.7f)
            : CardVisual::GetCardColor(cards[i]->GetData()->type, false);

        m_spriteRenderer->DrawSprite(m_whiteTexture, drawX, drawY,
            CARD_HOVER_W, CARD_HOVER_H, 0.0f, color);
    }

    if (ctx.selectedCardIndex >= 0 && ctx.selectedCardIndex != ctx.hoveredCardIndex
        && ctx.selectedCardIndex < (int)cards.size())
    {
        int i = ctx.selectedCardIndex;
        if (i >= (int)m_cardAnims.size()) return;  // もしくは処理をスキップ
        float cardX = m_cardAnims[i].currentX;

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = m_cardAnims[i].currentY;

        m_spriteRenderer->DrawSprite(m_whiteTexture, drawX, drawY,
            CARD_HOVER_W, CARD_HOVER_H, 0.0f,
            XMFLOAT4(0.8f, 0.8f, 0.0f, 0.7f));
    }

    float drawPileX = 20.0f;
    float drawPileY = m_screenHeight - 60.0f;
    float drawPileW = 50.0f;
    float drawPileH = 40.0f;
    bool hoverDrawPile = ctx.mousePos.x >= drawPileX && ctx.mousePos.x <= drawPileX + drawPileW
        && ctx.mousePos.y >= drawPileY && ctx.mousePos.y <= drawPileY + drawPileH;
    XMFLOAT4 drawPileColor = hoverDrawPile
        ? XMFLOAT4(0.3f, 0.3f, 0.9f, 1.0f)
        : XMFLOAT4(0.2f, 0.2f, 0.6f, 1.0f);
    m_spriteRenderer->DrawSprite(m_whiteTexture, drawPileX, drawPileY,
        drawPileW, drawPileH, 0.0f, drawPileColor);

    float discardX = 80.0f;
    float discardY = m_screenHeight - 60.0f;
    float discardW = 50.0f;
    float discardH = 40.0f;
    bool hoverDiscard = ctx.mousePos.x >= discardX && ctx.mousePos.x <= discardX + discardW
        && ctx.mousePos.y >= discardY && ctx.mousePos.y <= discardY + discardH;
    XMFLOAT4 discardColor = hoverDiscard
        ? XMFLOAT4(0.8f, 0.3f, 0.3f, 1.0f)
        : XMFLOAT4(0.5f, 0.2f, 0.2f, 1.0f);
    m_spriteRenderer->DrawSprite(m_whiteTexture, discardX, discardY,
        discardW, discardH, 0.0f, discardColor);

    if (ctx.deck->GetExhaustPileCount() > 0)
    {
        float exhaustX = 140.0f;
        float exhaustY = m_screenHeight - 60.0f;
        float exhaustW = 50.0f;
        float exhaustH = 40.0f;
        bool hoverExhaust = ctx.mousePos.x >= exhaustX && ctx.mousePos.x <= exhaustX + exhaustW
            && ctx.mousePos.y >= exhaustY && ctx.mousePos.y <= exhaustY + exhaustH;
        XMFLOAT4 exhaustColor = hoverExhaust
            ? XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f)
            : XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, exhaustX, exhaustY,
            exhaustW, exhaustH, 0.0f, exhaustColor);
    }

    DrawCardEffects();
    DrawPlayCardEffects();
    DrawDiscardEffects();

    m_spriteRenderer->End();

    m_spriteRenderer->Begin();
    DrawTargetIndicators(ctx);
    DrawPlayerOffScreenIndicator(ctx);
    m_spriteRenderer->End();

    m_textRenderer->Begin();

    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i >= (int)m_cardAnims.size()) continue;
        if (i == ctx.hoveredCardIndex) continue;
        if (i == ctx.selectedCardIndex) continue;

        float cardX = m_cardAnims[i].currentX;

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            cardX, m_cardAnims[i].currentY + 5.0f, 12.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }

    if (ctx.selectedCardIndex >= 0 && ctx.selectedCardIndex != ctx.hoveredCardIndex
        && ctx.selectedCardIndex < (int)cards.size())
    {
        int i = ctx.selectedCardIndex;
        if (i >= (int)m_cardAnims.size()) return;  // もしくは処理をスキップ
        float cardX = m_cardAnims[i].currentX;
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = m_cardAnims[i].currentY;

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            drawX + 5.0f, drawY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        wchar_t costText[32];
        swprintf_s(costText, L"Cost: %d", cards[i]->GetData()->cost);
        m_textRenderer->DrawText(costText,
            drawX + 5.0f, drawY + 32.0f, 13.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));

        D2D1_COLOR_F descColor = IsCardBoosted(cards[i]->GetData(), ctx.player)
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f)
            : D2D1::ColorF(D2D1::ColorF::LightGray);

        m_textRenderer->DrawText(
            GetCardEffectText(cards[i]->GetData(), ctx.player).c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f, descColor);
    }

    if (ctx.hoveredCardIndex >= 0 && ctx.hoveredCardIndex < (int)cards.size())
    {
        int i = ctx.hoveredCardIndex;
        if (i >= (int)m_cardAnims.size()) return;  // もしくは処理をスキップ
        float cardX = m_cardAnims[i].currentX;
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = m_cardAnims[i].currentY;

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            drawX + 5.0f, drawY + 10.0f, 16.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        wchar_t costText[32];
        swprintf_s(costText, L"Cost: %d", cards[i]->GetData()->cost);
        m_textRenderer->DrawText(costText,
            drawX + 5.0f, drawY + 32.0f, 13.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));

        D2D1_COLOR_F descColor = IsCardBoosted(cards[i]->GetData(), ctx.player)
            ? D2D1::ColorF(0.4f, 1.0f, 0.4f)
            : D2D1::ColorF(D2D1::ColorF::LightGray);

        m_textRenderer->DrawText(
            GetCardEffectText(cards[i]->GetData(), ctx.player).c_str(),
            drawX + 5.0f, drawY + 55.0f, 12.0f, descColor);
    }

    wchar_t drawText[32];
    swprintf_s(drawText, L"山:%d", ctx.deck->GetDrawPileCount());
    m_textRenderer->DrawText(drawText, drawPileX + 5.0f, drawPileY + 8.0f, 18.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    wchar_t discardText[32];
    swprintf_s(discardText, L"捨:%d", ctx.deck->GetDiscardPileCount());
    m_textRenderer->DrawText(discardText, discardX + 5.0f, discardY + 8.0f, 18.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    if (ctx.deck->GetExhaustPileCount() > 0)
    {
        wchar_t exhaustText[32];
        swprintf_s(exhaustText, L"廃:%d", ctx.deck->GetExhaustPileCount());
        m_textRenderer->DrawText(exhaustText, 145.0f, m_screenHeight - 52.0f, 18.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }

    if (ctx.showDrawPile || ctx.showDiscardPile || ctx.showExhaustPile)
        DrawPileViewer(ctx);

    wchar_t hpText[64];
    swprintf_s(hpText, L"%d / %d", ctx.player->GetHp(), ctx.player->GetMaxHp());
    m_textRenderer->DrawText(hpText, 20.0f, 2.0f, 45.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    wchar_t energyText[64];
    swprintf_s(energyText, L"Energy: %d / %d", ctx.player->GetEnergy(), ctx.player->GetMaxEnergy());
    m_textRenderer->DrawText(energyText, 20.0f, 100.0f, 20.0f,
        D2D1::ColorF(D2D1::ColorF::Yellow));

    if (ctx.player->GetBlock() > 0)
    {
        float pIconSize = 30.0f * 1.5f;
        float pIconX = 20.0f - pIconSize * 0.35f;
        float pIconY = 60.0f + (30.0f - pIconSize) / 2.0f;
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
    if (!ctx.showDrawPile && !ctx.showDiscardPile)
    {
        m_textRenderer->End();
        m_spriteRenderer->Begin();

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

            HPBarInfo eBar;
            eBar.currentHP = enemy->GetHp();
            eBar.maxHP = enemy->GetMaxHp();
            eBar.displayHP = enemy->GetDisplayHp();
            eBar.poisonDmg = enemy->GetBuffManager().GetTurnEndDamage().total();
            eBar.hasBurn = enemy->GetBuffManager().HasBuff(BuffType::Burn);
            DrawHPBar(barX, barY, barWidth, barHeight, eBar, ctx.highlightTimer);

            // ブロックアイコン（HPバー左端）
            if (enemy->GetBlock() > 0)
            {
                float iconSize = barHeight * 1.5f;
                float iconX = barX - iconSize / 2.0f;
                float iconY = barY + (barHeight - iconSize) / 2.0f;
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    iconX - 1.0f, iconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f,
                    0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    iconX, iconY, iconSize, iconSize,
                    0.0f, XMFLOAT4(0.3f, 0.6f, 1.0f, 1.0f));
            }

            // --- バフ/デバフアイコン（HPバーの下） ---
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
                XMFLOAT4 buffColor = BuffInfo::Get(buff.type).color;
                bool iconHover = (ctx.mousePos.x >= buffIconX && ctx.mousePos.x <= buffIconX + iconSize
                    && ctx.mousePos.y >= buffIconY && ctx.mousePos.y <= buffIconY + iconSize);
                if (iconHover)
                {
                    m_hasHoveredBuff = true;
                    m_hoveredBuffType = buff.type;
                    m_hoveredBuffValue = buff.value;
                    m_hoveredBuffX = buffIconX;
                    m_hoveredBuffY = buffIconY;
                }
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX - 1.0f, buffIconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f,
                    0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX, buffIconY, iconSize, iconSize, 0.0f,
                    iconHover ? XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) : buffColor);
                buffIconX += iconSize + 20.0f;
            }

            // --- 次の行動アイコン（頭上） ---
            const EnemyAction* action = enemy->GetNextAction();
            if (action)
            {
                float iconSize = 18.0f;
                float iconX = barX;
                float iconY = barY - iconSize - 2.0f;

                XMFLOAT4 iconColor;
                switch (action->type)
                {
                case EnemyActionType::Attack: iconColor = XMFLOAT4(0.9f, 0.2f, 0.2f, 1.0f); break;
                case EnemyActionType::Defend: iconColor = XMFLOAT4(0.2f, 0.4f, 0.9f, 1.0f); break;
                case EnemyActionType::Move:   iconColor = XMFLOAT4(0.2f, 0.8f, 0.3f, 1.0f); break;
                case EnemyActionType::Buf:    iconColor = XMFLOAT4(1.0f, 0.8f, 0.0f, 1.0f); break;
                case EnemyActionType::Debuf:  iconColor = XMFLOAT4(0.6f, 0.0f, 0.8f, 1.0f); break;
                default:                      iconColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f); break;
                }
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    iconX - 1.0f, iconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f,
                    0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    iconX, iconY, iconSize, iconSize, 0.0f, iconColor);
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

            // HP数値
            wchar_t hpText[32];
            swprintf_s(hpText, L"%d/%d", enemy->GetHp(), enemy->GetMaxHp());
            float textW = wcslen(hpText) * fontSize * 0.5f;
            float textX = barX + (barWidth - textW) / 2.0f;
            float textY = barY + (barHeight - fontSize) / 2.0f - 3.0;
            m_textRenderer->DrawText(hpText, textX + 1.0f, textY + 1.0f,
                fontSize, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
            m_textRenderer->DrawText(hpText, textX, textY,
                fontSize, D2D1::ColorF(D2D1::ColorF::White));

            // ブロック数値
            if (enemy->GetBlock() > 0)
            {
                float iconSize = barHeight * 1.5f;
                float iconX = barX - iconSize / 2.0f;
                float iconY = barY + (barHeight - iconSize) / 2.0f - 2.0;
                wchar_t blockText[16];
                swprintf_s(blockText, L"%d", enemy->GetBlock());
                float blockFontSize = max(7.0f, fontSize * 0.9f);
                float bTextW = wcslen(blockText) * blockFontSize * 0.5f;
                m_textRenderer->DrawText(blockText,
                    iconX + (iconSize - bTextW) / 2.0f + 1.0f,
                    iconY + (iconSize - blockFontSize) / 2.0f + 1.0f,
                    blockFontSize, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
                m_textRenderer->DrawText(blockText,
                    iconX + (iconSize - bTextW) / 2.0f,
                    iconY + (iconSize - blockFontSize) / 2.0f,
                    blockFontSize, D2D1::ColorF(D2D1::ColorF::White));
            }

            // 行動の数値（頭上）
            const EnemyAction* action = enemy->GetNextAction();
            if (action && action->value > 0 && action->type != EnemyActionType::Move)
            {
                float iconSize = 18.0f;
                float iconX = barX;
                float iconY = barY - iconSize - 2.0f;
                wchar_t valueBuf[16];
                swprintf_s(valueBuf, L"%d", action->value);
                m_textRenderer->DrawText(valueBuf,
                    iconX + iconSize + 3.0f + 1.0f, iconY + 1.0f + 1.0f,
                    14.0f, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
                m_textRenderer->DrawText(valueBuf,
                    iconX + iconSize + 3.0f, iconY + 1.0f,
                    14.0f, D2D1::ColorF(D2D1::ColorF::White));
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
                swprintf_s(buffVal, L"%d", buff.value);
                m_textRenderer->DrawText(buffVal,
                    buffIconX + iconSize + 2.0f, buffIconY + 1.0f,
                    11.0f, D2D1::ColorF(D2D1::ColorF::White));
                buffIconX += iconSize + 20.0f;
            }
        }
    }

    if (ctx.turnManager->IsPlayerTurn())
        m_textRenderer->DrawText(L"プレイヤーターン", 500.0f, 20.0f, 24.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    else
        m_textRenderer->DrawText(L"敵ターン", 500.0f, 20.0f, 24.0f,
            D2D1::ColorF(D2D1::ColorF::Red));

    const auto& buffs = ctx.player->GetBuffManager().GetBuffs();
    float buffY = 155.0f;
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
        if (buff.isPermanent())
            swprintf_s(buffText, L"%s %d", info.name.c_str(), buff.value);
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
            std::wstring desc = BuffInfo::GetDescription(buff.type, buff.value);
            m_textRenderer->DrawText(desc.c_str(), 40.0f, buffY, 12.0f,
                D2D1::ColorF(0.8f, 0.8f, 0.8f));
            buffY += 18.0f;
        }
    }

    // ターンエンドボタン
    if (ctx.battleResult == BattleResult::None && ctx.isPlayerTurn)
    {
        float btnX = ctx.screenWidth - 160.0f;
        float btnY = ctx.screenHeight - 60.0f;
        float btnW = 140.0f;
        float btnH = 40.0f;

        bool hoverEnd = ctx.mousePos.x >= btnX && ctx.mousePos.x <= btnX + btnW
            && ctx.mousePos.y >= btnY && ctx.mousePos.y <= btnY + btnH;
        XMFLOAT4 btnColor = hoverEnd
            ? XMFLOAT4(0.3f, 0.7f, 1.0f, 1.0f)
            : XMFLOAT4(0.2f, 0.5f, 0.8f, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, btnX, btnY, btnW, btnH, 0.0f, btnColor);
        m_textRenderer->DrawText(L"ターンエンド", btnX + 10.0f, btnY + 10.0f, 0.4f);
    }

    if (ctx.battleResult == BattleResult::Win)
    {
        m_textRenderer->DrawText(L"Victory!",
            m_screenWidth / 2.0f - 80.0f, m_screenHeight / 2.0f - 30.0f,
            60.0f, D2D1::ColorF(D2D1::ColorF::Gold));
        m_textRenderer->DrawText(L"クリックで次へ",
            m_screenWidth / 2.0f - 80.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
    }
    else if (ctx.battleResult == BattleResult::Lose)
    {
        m_textRenderer->DrawText(L"Game Over...",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f - 30.0f,
            60.0f, D2D1::ColorF(D2D1::ColorF::Red));
        m_textRenderer->DrawText(L"クリックでタイトルへ",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
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
        m_spriteRenderer->DrawSprite(m_whiteTexture, tipX, tipY, tipW, tipH, 0.0f,
            XMFLOAT4(0.08f, 0.08f, 0.15f, 0.95f));
        m_spriteRenderer->End();
        m_textRenderer->Begin();

        m_textRenderer->DrawText(info.name.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
            D2D1::ColorF(1.0f, 1.0f, 0.5f));
        m_textRenderer->DrawText(desc.c_str(), tipX + 5.0f, tipY + 18.0f, 11.0f,
            D2D1::ColorF(0.8f, 0.8f, 0.8f));
    }

    // 罠のホバー詳細
    if (ctx.hoveredCell.first >= 0 && ctx.hoveredCell.second >= 0
        && ctx.hoveredCell.first < ctx.gridMap->GetCols()
        && ctx.hoveredCell.second < ctx.gridMap->GetRows())
    {
        auto& hCell = ctx.gridMap->GetCell(ctx.hoveredCell.first, ctx.hoveredCell.second);
        if (hCell.trap.active)
        {
            std::wstring trapName;
            XMFLOAT4 trapColor;
            switch (hCell.trap.type)
            {
            case TrapType::Explosion:
                trapName = L"爆発の罠";
                trapColor = XMFLOAT4(1.0f, 0.3f, 0.0f, 1.0f);
                break;
            case TrapType::Root:
                trapName = L"拘束の罠";
                trapColor = XMFLOAT4(0.2f, 0.8f, 0.2f, 1.0f);
                break;
            case TrapType::Poison:
                trapName = L"毒の罠";
                trapColor = XMFLOAT4(0.5f, 0.0f, 0.8f, 1.0f);
                break;
            default:
                trapName = L"罠";
                trapColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
                break;
            }

            wchar_t detailText[64];
            if (hCell.trap.type == TrapType::Explosion)
                swprintf_s(detailText, L"ダメージ: %d", hCell.trap.value);
            else
                swprintf_s(detailText, L"効果: %d (%dT)", hCell.trap.value, hCell.trap.duration);

            float tipX = (float)ctx.mousePos.x + 15.0f;
            float tipY = (float)ctx.mousePos.y - 50.0f;
            float tipW = 150.0f;
            float tipH = 40.0f;

            m_textRenderer->End();
            m_spriteRenderer->Begin();
            m_spriteRenderer->DrawSprite(m_whiteTexture, tipX, tipY, tipW, tipH, 0.0f,
                XMFLOAT4(0.08f, 0.08f, 0.15f, 0.95f));
            m_spriteRenderer->End();
            m_textRenderer->Begin();

            m_textRenderer->DrawText(trapName.c_str(), tipX + 5.0f, tipY + 2.0f, 13.0f,
                D2D1::ColorF(trapColor.x, trapColor.y, trapColor.z));
            m_textRenderer->DrawText(detailText, tipX + 5.0f, tipY + 20.0f, 11.0f,
                D2D1::ColorF(D2D1::ColorF::LightGray));
        }
    }

    m_textRenderer->End();

    m_spriteRenderer->Begin();
    DrawEnemyInfoPanel(ctx);
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
                        int minDist = INT_MAX;
                        for (auto& [dc2, dr2] : enemy->GetGridShape())
                        {
                            int dist = abs(ctx.playerCol - (enemy->gridCol + dc2))
                                + abs(ctx.playerRow - (enemy->gridRow + dr2));
                            minDist = min(minDist, dist);
                        }
                        if (minDist <= data->range)
                            inRange = true;
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
        XMFLOAT4 arrowColor(0.2f, 0.8f, 1.0f, 1.0f);

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
            if (cell.trap.active)
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
        if (ctx.hoveredCell.first < 0) return;

        auto& cell = ctx.gridMap->GetCell(ctx.hoveredCell.first, ctx.hoveredCell.second);
        if (cell.type != CellType::Empty) return;

        int dc = abs(ctx.playerCol - ctx.hoveredCell.first);
        int dr = abs(ctx.playerRow - ctx.hoveredCell.second);
        if ((dc + dr) > data->range) return;

        XMFLOAT4 arrowColor(0.2f, 0.9f, 0.3f, 1.0f);

        float wx, wz;
        GridToWorld(ctx.gridMap, ctx.hoveredCell.first, ctx.hoveredCell.second, wx, wz);

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
    const auto& pile = ctx.showDrawPile
        ? ctx.deck->GetDrawPile()
        : ctx.showDiscardPile
        ? ctx.deck->GetDiscardPile()
        : ctx.deck->GetExhaustPile();

    const wchar_t* title = ctx.showDrawPile ? L"山札"
        : ctx.showDiscardPile ? L"捨て札"
        : L"廃棄札";

    float bgX = m_screenWidth / 2.0f - 300.0f;
    float bgY = 50.0f;
    float bgW = 600.0f;
    float bgH = 580.0f;

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, bgX, bgY, bgW, bgH,
        0.0f, XMFLOAT4(0.1f, 0.1f, 0.1f, 0.95f));
    m_spriteRenderer->End();

    m_textRenderer->DrawText(title, bgX + 20.0f, bgY + 10.0f, 24.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    m_textRenderer->DrawText(L"[枠外クリックで閉じる]", bgX + bgW - 150.0f, bgY + 10.0f, 16.0f,
        D2D1::ColorF(D2D1::ColorF::Gray));

    std::vector<std::string> displayPile = pile;

    if (ctx.showDrawPile)
        std::sort(displayPile.begin(), displayPile.end());

    const float cardW = 160.0f;
    const float cardH = 50.0f;
    const float padding = 10.0f;
    const int   cols = 3;

    for (int i = 0; i < (int)displayPile.size(); i++)
    {
        const CardData* data = CardDataBase::Get(displayPile[i]);
        if (!data) continue;

        int col = i % cols;
        int row = i / cols;

        float cx = bgX + 20.0f + col * (cardW + padding);
        float cy = bgY + 50.0f + row * (cardH + padding);

        XMFLOAT4 cardColor;
        switch (data->type)
        {
        case CardType::Attack: cardColor = XMFLOAT4(0.5f, 0.1f, 0.1f, 1.0f); break;
        case CardType::Skill:  cardColor = XMFLOAT4(0.1f, 0.3f, 0.5f, 1.0f); break;
        case CardType::Move:   cardColor = XMFLOAT4(0.1f, 0.4f, 0.2f, 1.0f); break;
        case CardType::Power:  cardColor = XMFLOAT4(0.4f, 0.1f, 0.4f, 1.0f); break;
        default:               cardColor = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f); break;
        }

        m_spriteRenderer->Begin();
        m_spriteRenderer->DrawSprite(m_whiteTexture, cx, cy, cardW, cardH,
            0.0f, cardColor);
        m_spriteRenderer->End();

        wchar_t cardText[64];
        swprintf_s(cardText, L"%s  Cost:%d", data->name.c_str(), data->cost);
        m_textRenderer->DrawText(cardText, cx + 5.0f, cy + 5.0f, 14.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        m_textRenderer->DrawText(
            GetCardEffectText(data, ctx.player).c_str(),
            cx + 5.0f, cy + 26.0f, 11.0f,
            D2D1::ColorF(D2D1::ColorF::LightGray));
    }
}

void BattleUI::StartDrawCardEffect(const std::string& cardId)
{
    DrawCardEffect effect;
    effect.cardId = cardId;
    effect.x = m_screenWidth - 120.0f;
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

        effect.x = m_screenWidth - 120.0f + (effect.targetX - (m_screenWidth - 120.0f)) * ease;
        effect.y = m_screenHeight - 60.0f + (effect.targetY - (m_screenHeight - 60.0f)) * ease;
        effect.alpha = 1.0f - t * 0.5f;

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
        m_spriteRenderer->DrawSprite(m_whiteTexture,
            effect.x, effect.y, CARD_WIDTH, CARD_HEIGHT, 0.0f,
            XMFLOAT4(0.2f, 0.4f, 0.8f, effect.alpha));
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

void BattleUI::UpdateDiscardEffects(float deltaTime)
{
    for (auto& e : m_discardCardEffects)
    {
        if (e.done) continue;
        e.timer += deltaTime;
        float t = min(1.0f, e.timer / 1.0f);
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
        float t = min(1.0f, e.timer / 1.0f);
        float ease = t * t;
        float x = e.startX + (targetX - e.startX) * ease;
        float y = e.startY + (targetY - e.startY) * ease;

        XMFLOAT4 color(0.4f, 0.4f, 0.4f, e.alpha);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y,
            CARD_WIDTH, CARD_HEIGHT, 0.0f, color);
    }
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

void BattleUI::UpdatePlayCardEffects(float deltaTime)
{
    for (auto& e : m_playCardEffects)
    {
        if (e.done) continue;
        e.timer += deltaTime;
        float t = min(1.0f, e.timer / 2.0f);
        e.alpha = 1.0f - t;
        if (t >= 1.0f) e.done = true;
    }
    m_playCardEffects.erase(
        std::remove_if(m_playCardEffects.begin(), m_playCardEffects.end(),
            [](const PlayCardEffect& e) { return e.done; }),
        m_playCardEffects.end());
}

void BattleUI::UpdateCardAnimations(float deltaTime, int handSize, int hoveredIndex, 
    int selectedIndex, POINT mousePos, bool selectedNeedsTarget)
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
    float speed = 1.0f;
    float dt = min(deltaTime, 0.03f);

    for (int i = 0; i < handSize; i++)
    {
        float targetX = m_screenWidth / 2.0f
            - (handSize * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float targetY;

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

void BattleUI::DrawPlayCardEffects()
{
    for (auto& e : m_playCardEffects)
    {
        float t = min(1.0f, e.timer / 2.0f);
        float ease = 1.0f - (1.0f - t) * (1.0f - t);
        float y = e.startY - 150.0f * ease;
        float scale = 1.0f + 0.3f * ease;
        float w = CARD_WIDTH * scale;
        float h = CARD_HEIGHT * scale;
        float x = e.startX - (w - CARD_WIDTH) / 2.0f;

        XMFLOAT4 color = CardVisual::GetCardColor(e.cardType, false);
        color.w = e.alpha;
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h, 0.0f, color);
    }
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
    float panelY = 10.0f;
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
        if (enemy->GetNextAction()) detailH += 22.0f;
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

        XMFLOAT4 bgColor = isHover
            ? XMFLOAT4(0.45f, 0.45f, 0.25f, 0.9f)
            : XMFLOAT4(0.45f, 0.45f, 0.25f, 0.7f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, panelX, entryY,
            panelW, entryH, 0.0f, bgColor);

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
        panelBar.poisonDmg = enemy->GetBuffManager().GetTurnEndDamage().total();
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
        if (hEnemy->GetNextAction()) detailH += 22.0f;
        if (hEnemy->GetBlock() > 0) detailH += 18.0f;
        if (hEnemy->IsImmovable()) detailH += 20.0f;
        for (auto& buff : hEnemy->GetBuffManager().GetBuffs())
            detailH += 20.0f;
        detailH += 18.0f;
        if (detailH < entryH) detailH = entryH;
        m_spriteRenderer->DrawSprite(m_whiteTexture, detailX, detailEntryY,
            190.0f, detailH, 0.0f, XMFLOAT4(0.1f, 0.1f, 0.2f, 1.0f));
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
            std::wstring dispText = action->description;
            if (action->type == EnemyActionType::Attack)
            {
                int finalVal = enemy->GetBuffManager().GetFinalAttack(action->value);
                std::wstring oldNum = std::to_wstring(action->value);
                std::wstring newNum = std::to_wstring(finalVal);
                size_t pos = dispText.find(oldNum);
                if (pos != std::wstring::npos)
                    dispText.replace(pos, oldNum.size(), newNum);
            }
            else if (action->type == EnemyActionType::Defend)
            {
                int finalVal = enemy->GetBuffManager().GetFinalBlock(action->value);
                std::wstring oldNum = std::to_wstring(action->value);
                std::wstring newNum = std::to_wstring(finalVal);
                size_t pos = dispText.find(oldNum);
                if (pos != std::wstring::npos)
                    dispText.replace(pos, oldNum.size(), newNum);
            }
            m_textRenderer->DrawText(dispText.c_str(),
                panelX + iconSize + 10.0f, entryY + 32.0f, 14.0f,
                D2D1::ColorF(D2D1::ColorF::Orange));
        }

        // ホバー中の詳細テキスト
        if (hoveredEnemy == i)
        {
            float detailX = panelX - 200.0f;
            float lineY = entryY + 5.0f;

            const EnemyAction* act = enemy->GetNextAction();
            if (act)
            {
                int displayValue = act->value;
                if (act->type == EnemyActionType::Attack)
                    displayValue = enemy->GetBuffManager().GetFinalAttack(act->value);
                else if (act->type == EnemyActionType::Defend)
                    displayValue = enemy->GetBuffManager().GetFinalBlock(act->value);
                std::wstring actionText = act->description + L": " + std::to_wstring(displayValue);
                m_textRenderer->DrawText(actionText.c_str(),
                    detailX + 10.0f, lineY, 15.0f,
                    D2D1::ColorF(1.0f, 0.8f, 0.3f));
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
                std::wstring buffText = info.name + L": " + std::to_wstring(buff.value);
                if (!buff.isPermanent())
                    buffText += L" (" + std::to_wstring(buff.duration) + L"T)";
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
                    std::wstring desc = BuffInfo::GetDescription(buff.type, buff.value);
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

void BattleUI::DrawEnemyGridHighlight(const BattleUIContext& ctx)
{
    if (m_panelHoveredEnemy < 0) return;
    if (m_panelHoveredEnemy >= (int)ctx.enemies->size()) return;

    Enemy* enemy = (*ctx.enemies)[m_panelHoveredEnemy];
    float footX, footY;
    if (!GetEnemyFootPos(enemy, ctx.renderer3D, footX, footY)) return;

    float scale = 1.0f / ctx.cameraZoom;
    float size = 70.0f * scale;
    float thickness = 3.0f * scale;
    XMFLOAT4 color(1.0f, 1.0f, 0.0f, 0.8f);

    float left = footX - size / 2.0f;
    float top = footY - size;

    // 上
    m_spriteRenderer->DrawSprite(m_whiteTexture, left, top, size, thickness, 0.0f, color);
    // 下
    m_spriteRenderer->DrawSprite(m_whiteTexture, left, top + size, size, thickness, 0.0f, color);
    // 左
    m_spriteRenderer->DrawSprite(m_whiteTexture, left, top, thickness, size, 0.0f, color);
    // 右
    m_spriteRenderer->DrawSprite(m_whiteTexture, left + size - thickness, top, thickness, size + thickness, 0.0f, color);
}