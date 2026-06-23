#include "BattleUI.h"
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

void BattleUI::DrawHPBar(float x, float y, float w, float h, int currentHP, int maxHP)
{
    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h,
        0.0f, XMFLOAT4(0.3f, 0.0f, 0.0f, 1.0f));

    float ratio = (float)currentHP / (float)maxHP;
    m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w * ratio, h,
        0.0f, XMFLOAT4(0.0f, 0.8f, 0.0f, 1.0f));
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
        actualValue = player->GetBuffManager().ApplyAttackBuff(data->mainEffect.value);
    else if (data->type == CardType::Skill)
        actualValue = player->GetBuffManager().ApplyBlockBuff(data->mainEffect.value);

    std::wstring result = data->description;
    std::wstring placeholder = L"{value}";
    size_t pos = result.find(placeholder);
    if (pos != std::wstring::npos)
        result.replace(pos, placeholder.size(), std::to_wstring(actualValue));

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

    m_spriteRenderer->Begin();


    DrawHPBar(20.0f, 60.0f, 200.0f, 30.0f, ctx.player->GetHp(), ctx.player->GetMaxHp());
    DrawEnemyInfoPanel(ctx);

    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i == ctx.hoveredCardIndex) continue;
        if (i == ctx.selectedCardIndex) continue;

        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawY = cardHideY;

        XMFLOAT4 color = CardVisual::GetCardColor(
            cards[i]->GetData()->type, false);

        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, drawY,
            CARD_WIDTH, CARD_HEIGHT, 0.0f, color);
    }

    if (ctx.hoveredCardIndex >= 0 && ctx.hoveredCardIndex < (int)cards.size())
    {
        int i = ctx.hoveredCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY;

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
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY + 40.0f;

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

    DrawCardEffects();

    m_spriteRenderer->End();

    m_spriteRenderer->Begin();
    DrawTargetIndicators(ctx);
    DrawPlayerOffScreenIndicator(ctx);
    m_spriteRenderer->End();

    m_textRenderer->Begin();

    for (int i = 0; i < (int)cards.size(); i++)
    {
        if (i == ctx.hoveredCardIndex) continue;
        if (i == ctx.selectedCardIndex) continue;

        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);

        m_textRenderer->DrawText(cards[i]->GetData()->name.c_str(),
            cardX, cardHideY + 5.0f, 12.0f,
            D2D1::ColorF(D2D1::ColorF::White));
    }

    if (ctx.selectedCardIndex >= 0 && ctx.selectedCardIndex != ctx.hoveredCardIndex
        && ctx.selectedCardIndex < (int)cards.size())
    {
        int i = ctx.selectedCardIndex;
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY + 40.0f;

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
        float cardX = m_screenWidth / 2.0f
            - (cards.size() * (CARD_WIDTH + 10.0f)) / 2.0f
            + i * (CARD_WIDTH + 10.0f);
        float drawX = cardX - (CARD_HOVER_W - CARD_WIDTH) / 2.0f;
        float drawY = cardHoverY;

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

    if (ctx.showDrawPile || ctx.showDiscardPile)
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
        wchar_t blockText[64];
        swprintf_s(blockText, L"Block: %d", ctx.player->GetBlock());
        m_textRenderer->DrawText(blockText, 20.0f, 125.0f, 20.0f,
            D2D1::ColorF(D2D1::ColorF::LightBlue));
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
            float barHeight = (enemy->IsBoss() ? 15.0f : 10.0f) * scale;

            // --- HPバー（足元の少し下） ---
            float barX = footX - barWidth / 2.0f;
            float barY = footY - 30.0f;

            m_spriteRenderer->DrawSprite(m_whiteTexture,
                barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f,
                0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
            m_spriteRenderer->DrawSprite(m_whiteTexture,
                barX, barY, barWidth, barHeight,
                0.0f, XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f));

            float ratio = (float)enemy->GetHp() / (float)enemy->GetMaxHp();
            XMFLOAT4 barColor = ratio > 0.5f
                ? XMFLOAT4(0.0f, 0.0f, 0.8f, 1.0f)
                : ratio > 0.25f
                ? XMFLOAT4(0.8f, 0.8f, 0.0f, 1.0f)
                : XMFLOAT4(0.8f, 0.0f, 0.0f, 1.0f);
            m_spriteRenderer->DrawSprite(m_whiteTexture,
                barX, barY, barWidth * ratio, barHeight, 0.0f, barColor);

            // --- バフ/デバフアイコン（HPバーの下） ---
            float buffIconY = barY + barHeight + 4.0f;
            float buffIconX = barX;
            for (auto& buff : enemy->GetBuffManager().GetBuffs())
            {
                float iconSize = 16.0f;
                XMFLOAT4 buffColor;
                switch (buff.type)
                {
                case BuffType::Poison:      buffColor = XMFLOAT4(0.5f, 0.0f, 0.8f, 1.0f); break;
                case BuffType::AttackUp:    buffColor = XMFLOAT4(0.9f, 0.3f, 0.1f, 1.0f); break;
                case BuffType::DefenseUp:   buffColor = XMFLOAT4(0.2f, 0.5f, 0.9f, 1.0f); break;
                default:                    buffColor = XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f); break;
                }
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX - 1.0f, buffIconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f,
                    0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX, buffIconY, iconSize, iconSize, 0.0f, buffColor);
                buffIconX += iconSize + 4.0f;
            }

            if (enemy->GetBlock() > 0)
            {
                float iconSize = 16.0f;
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX - 1.0f, buffIconY - 1.0f, iconSize + 2.0f, iconSize + 2.0f,
                    0.0f, XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
                m_spriteRenderer->DrawSprite(m_whiteTexture,
                    buffIconX, buffIconY, iconSize, iconSize,
                    0.0f, XMFLOAT4(0.2f, 0.5f, 0.9f, 1.0f));
                buffIconX += iconSize + 4.0f;
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
            swprintf_s(hpText, L"%d / %d", enemy->GetHp(), enemy->GetMaxHp());
            m_textRenderer->DrawText(hpText, barX + 3.0f + 1.0f, barY + 1.0f,
                fontSize, D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f));
            m_textRenderer->DrawText(hpText, barX + 3.0f, barY,
                fontSize, D2D1::ColorF(D2D1::ColorF::White));

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
            for (auto& buff : enemy->GetBuffManager().GetBuffs())
            {
                float iconSize = 16.0f;
                wchar_t buffVal[16];
                swprintf_s(buffVal, L"%d", buff.value);
                m_textRenderer->DrawText(buffVal,
                    buffIconX + iconSize + 2.0f, buffIconY + 1.0f,
                    11.0f, D2D1::ColorF(D2D1::ColorF::White));
                buffIconX += iconSize + 4.0f;
            }

            if (enemy->GetBlock() > 0)
            {
                float iconSize = 16.0f;
                wchar_t blockText[16];
                swprintf_s(blockText, L"%d", enemy->GetBlock());
                m_textRenderer->DrawText(blockText,
                    buffIconX + iconSize + 2.0f, buffIconY + 1.0f,
                    11.0f, D2D1::ColorF(D2D1::ColorF::White));
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
        wchar_t buffText[64];
        if (buff.isPermanent())
            swprintf_s(buffText, L"%s +%d (永続)", buff.name.c_str(), buff.value);
        else
            swprintf_s(buffText, L"%s +%d (%dターン)", buff.name.c_str(), buff.value, buff.duration);

        m_textRenderer->DrawText(buffText, 20.0f, buffY, 16.0f,
            D2D1::ColorF(D2D1::ColorF::LightGreen));
        buffY += 22.0f;
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
        m_textRenderer->DrawText(L"Enterキーで次へ",
            m_screenWidth / 2.0f - 80.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
    }
    else if (ctx.battleResult == BattleResult::Lose)
    {
        m_textRenderer->DrawText(L"Game Over...",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f - 30.0f,
            60.0f, D2D1::ColorF(D2D1::ColorF::Red));
        m_textRenderer->DrawText(L"Enterキーでタイトルへ",
            m_screenWidth / 2.0f - 100.0f, m_screenHeight / 2.0f + 40.0f,
            24.0f, D2D1::ColorF(D2D1::ColorF::White));
    }

    m_textRenderer->End();
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

    float cardAreaY = m_screenHeight - CARD_HEIGHT - CARD_HIDE_Y_OFFSET;
    if (sy > cardAreaY)
        return;

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
        : ctx.deck->GetDiscardPile();

    const wchar_t* title = ctx.showDrawPile ? L"山札" : L"捨て札";

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
        bool isHover = mp.x >= panelX && mp.x <= panelX + panelW
            && mp.y >= entryY && mp.y <= entryY + entryH;

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
        float hpRatio = (float)enemy->GetHp() / (float)enemy->GetMaxHp();

        m_spriteRenderer->DrawSprite(m_whiteTexture, barX, barY,
            barW, barH, 0.0f, XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f));
        m_spriteRenderer->DrawSprite(m_whiteTexture, barX, barY,
            barW * hpRatio, barH, 0.0f, XMFLOAT4(0.0f, 0.0f, 0.8f, 1.0f));

        if (isHover)
            hoveredEnemy = i;


        // テキスト（スプライト描画後にテキスト描画）
        m_spriteRenderer->End();
        m_textRenderer->Begin();

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

            // 次の行動
            const EnemyAction* action = enemy->GetNextAction();
            if (action)
            {
                m_textRenderer->DrawText(action->description.c_str(),
                    panelX + iconSize + 10.0f, entryY + 32.0f, 14.0f,
                    D2D1::ColorF(D2D1::ColorF::Orange));
            }

            // ホバー時の詳細背景
            if (hoveredEnemy >= 0)
            {
                float entryY = panelY + hoveredEnemy * (entryH + 5.0f);
                float detailX = panelX - 200.0f;
                m_spriteRenderer->DrawSprite(m_whiteTexture, detailX, entryY,
                    190.0f, entryH, 0.0f, XMFLOAT4(0.1f, 0.1f, 0.2f, 0.85f));
            }
            m_spriteRenderer->End();

            // ホバー時の詳細
            if (hoveredEnemy == i)
            {
                float detailX = panelX - 200.0f;
                float detailY = entryY;
                float lineY = detailY + 5.0f;

                // 次の行動
                const EnemyAction* act = enemy->GetNextAction();
                if (act)
                {
                    std::wstring actionText = act->description + L": " + std::to_wstring(act->value);
                    m_textRenderer->DrawText(actionText.c_str(),
                        detailX + 10.0f, lineY, 15.0f,
                        D2D1::ColorF(1.0f, 0.8f, 0.3f));
                    lineY += 22.0f;
                }

                // ブロック
                if (enemy->GetBlock() > 0)
                {
                    wchar_t blockText[32];
                    swprintf_s(blockText, L"Block: %d", enemy->GetBlock());
                    m_textRenderer->DrawText(blockText,
                        detailX + 10.0f, lineY, 15.0f,
                        D2D1::ColorF(D2D1::ColorF::LightBlue));
                    lineY += 18.0f;
                }

                // バフ/デバフを自動列挙
                for (auto& buff : enemy->GetBuffManager().GetBuffs())
                {
                    std::wstring buffText = buff.name + L": " + std::to_wstring(buff.value);
                    if (!buff.isPermanent())
                        buffText += L" (" + std::to_wstring(buff.duration) + L"T)";

                    m_textRenderer->DrawText(buffText.c_str(),
                        detailX + 10.0f, lineY, 14.0f,
                        D2D1::ColorF(0.6f, 1.0f, 0.6f));
                    lineY += 20.0f;
                }
            }
        }

        m_textRenderer->End();
   
    }
}