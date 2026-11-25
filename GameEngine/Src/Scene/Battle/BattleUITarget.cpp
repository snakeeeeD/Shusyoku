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

// 狙いの表示（カードの狙い・敵の移動予定・画面の外のプレイヤーの印）
// （BattleUI のメンバー。BattleUI.cpp から機能ごとに分けたファイル）

void BattleUI::DrawArrowIndicator(float sx, float sy, const XMFLOAT4& color, float highlightTimer)
{
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

void BattleUI::DrawTargetIndicators(const BattleUIContext& ctx)
{
    if (ctx.selectedCardIndex < 0 || ctx.selectedCardIndex >= (int)ctx.hand->GetCards().size())
        return;

    const CardData* data = ctx.hand->GetCards()[ctx.selectedCardIndex]->GetData();
    if (!data) return;

    if (data->type == CardType::Attack)      DrawAttackTargets(ctx, data);
    else if (data->type == CardType::Skill || data->type == CardType::Power) DrawSkillTargets(ctx, data);
    else if (data->type == CardType::Move) DrawMoveTargets(ctx, data);
}

// 攻撃カードの狙い（突進・貫通は予告だけ、それ以外は当たる敵に矢印）
void BattleUI::DrawAttackTargets(const BattleUIContext& ctx, const CardData* data)
{
    if (data->dash && ctx.hoveredCell.first >= 0) { DrawDashPreview(ctx, data); return; }
    if (data->pierce && ctx.hoveredCell.first >= 0) { DrawPiercePreview(ctx, data); return; }
    DrawAttackArrows(ctx, data);
}

// 突進の予告：進む向きの列と、ぶつかる敵・止まるマス
void BattleUI::DrawDashPreview(const BattleUIContext& ctx, const CardData* data)
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
}

// 貫通の予告：列の敵すべてに矢印
void BattleUI::DrawPiercePreview(const BattleUIContext& ctx, const CardData* data)
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
}

// 攻撃が当たる敵の上に矢印
void BattleUI::DrawAttackArrows(const BattleUIContext& ctx, const CardData* data)
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
                        enemy->CenterWorld(preview.destCol, preview.destRow, ctx.gridMap, wx, wz); wx += 0.55f; wz += 0.55f;   // GridToWorld と同じ基準

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
                    auto aim = ctx.hoveredCell;                       // 敵の上ならそのマスを狙う
                    if (!enemy->OccupiesCell(aim.first, aim.second))
                        aim = { enemy->gridCol, enemy->gridRow };
                    auto preview = CardExecutor::PreviewPull(
                        enemy, ctx.playerCol, ctx.playerRow,
                        data->onHitEffect.value, ctx.gridMap, *ctx.enemies,
                        aim.first, aim.second);

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
                        enemy->CenterWorld(preview.destCol, preview.destRow, ctx.gridMap, wx, wz); wx += 0.55f; wz += 0.55f;   // GridToWorld と同じ基準

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

// スキル・パワーの狙い（青・紫の矢印）
void BattleUI::DrawSkillTargets(const BattleUIContext& ctx, const CardData* data)
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

    if (clip.w > 0.0f)
    {
        float sx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
        float sy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
        DrawArrowIndicator(sx, sy, arrowColor, ctx.highlightTimer);
    }
}

// 移動カードの狙い（引いた経路の先）
void BattleUI::DrawMoveTargets(const BattleUIContext& ctx, const CardData* data)
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

// 敵の移動予定の矢印（先に全員の経路を決める：先に決まった移動先を占有扱いにして、かぶりを避ける）
void BattleUI::DrawEnemyMoveArrows(const BattleUIContext& ctx)
{
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
            if (enemy->GetHp() <= 0 || enemy->IsMoving()) continue;   // 動き出したら矢印は消す

            const auto& path = enemy->GetPlannedMovePath();
            if (path.empty()) continue;

            XMFLOAT4 hue = enemy->hueColor;   // 範囲・パネルと同じ識別色（撃破で並びが変わってもずれない）
            auto proj = [&](float wx, float wz, float& sx, float& sy) -> bool {
                XMVECTOR wp = XMVectorSet(wx - 0.5f, 0.15f, wz - 0.5f, 1.0f);
                XMVECTOR cp = XMVector4Transform(wp, ctx.renderer3D->GetViewMatrix() * ctx.renderer3D->GetProjectionMatrix());
                XMFLOAT4 clip; XMStoreFloat4(&clip, cp);
                if (clip.w <= 0) return false;
                sx = (clip.x / clip.w + 1) * 0.5f * m_screenWidth;
                sy = (1 - clip.y / clip.w) * 0.5f * m_screenHeight;
                return true;
                };

            auto toArrow = [&](int c, int r, float& wx, float& wz) {   // マスの形の中心を矢印の座標で返す
                enemy->CenterWorld(c, r, ctx.gridMap, wx, wz);
                wx += 0.55f; wz += 0.55f;   // GridToWorld と同じ基準
                };

            // 敵の見た目マスが最終目的地に着いたら消す
            float gx, gz; enemy->CenterWorld(path.back().first, path.back().second, ctx.gridMap, gx, gz);
            if (fabsf(enemy->worldX - gx) < 0.55f && fabsf(enemy->worldZ - gz) < 0.55f) continue;

            // 始点：プレイヤーターン中はマス中心。敵ターン中は見た目位置を GridToWorld 系に変換
            float swx, swz;
            if (ctx.isPlayerTurn)
                toArrow(enemy->gridCol, enemy->gridRow, swx, swz);
            else
            {
                float fcol = enemy->worldX / 1.1f + ctx.gridMap->GetCols() / 2.0f;
                float frow = enemy->worldZ / 1.1f + ctx.gridMap->GetRows() / 2.0f;
                float w0x, w0z, w1x, w1z;
                GridToWorld(ctx.gridMap, 0, 0, w0x, w0z);
                GridToWorld(ctx.gridMap, 1, 1, w1x, w1z);
                swx = w0x + (w1x - w0x) * fcol;
                swz = w0z + (w1z - w0z) * frow;
            }
            float esx, esy; if (!proj(swx, swz, esx, esy)) continue;

            // 経路の各マスを画面座標へ（始点＝敵→各マス）＝角でカクッと折れるポリライン
            std::vector<std::pair<float, float>> pts;
            pts.push_back({ esx, esy });
            for (auto& cell : path)
            {
                float wx, wz; toArrow(cell.first, cell.second, wx, wz);
                float px, py; if (proj(wx, wz, px, py)) pts.push_back({ px, py });
            }
            if ((int)pts.size() < 2) continue;

            float totalL = 0.0f;
            for (int i = 1; i < (int)pts.size(); i++)
            {
                float dx = pts[i].first - pts[i - 1].first, dy = pts[i].second - pts[i - 1].second;
                totalL += sqrtf(dx * dx + dy * dy);
            }

            const float headLen = 26.0f, step = 26.0f, baseLen = 16.0f, baseThick = 13.0f;

            // 先端の三角（最終セグメントの向き）
            {
                int lp = (int)pts.size() - 1;
                float avx = pts[lp].first - pts[lp - 1].first, avy = pts[lp].second - pts[lp - 1].second;
                float ang = atan2f(avy, avx);
                float hw = 0.5f + 0.5f * sinf(ctx.highlightTimer * 2.5f - totalL * 0.035f);
                float hs = 40.0f + 8.0f * hw;
                m_spriteRenderer->DrawSprite(TextureManager::Get("ui_arrowhead"),
                    pts[lp].first - hs / 2, pts[lp].second - hs / 2, hs, hs, ang,
                    XMFLOAT4(hue.x, hue.y, hue.z, 0.75f + 0.25f * hw));
            }

            // シャフト＝ポリラインに沿った破線（各セグメントの向きで折れる）
            float scroll = fmodf(ctx.highlightTimer * 50.0f, step);
            float breath = 0.5f + 0.5f * sinf(ctx.highlightTimer * 3.0f);
            for (float d = 6.0f + scroll; d < totalL - headLen; d += step)
            {
                float acc = 0.0f;
                for (int si = 1; si < (int)pts.size(); si++)
                {
                    float dx = pts[si].first - pts[si - 1].first, dy = pts[si].second - pts[si - 1].second;
                    float sl = sqrtf(dx * dx + dy * dy); if (sl < 1e-3f) sl = 1e-3f;
                    if (acc + sl >= d)
                    {
                        float t = (d - acc) / sl;
                        float cx = pts[si - 1].first + dx * t;
                        float cy = pts[si - 1].second + dy * t;
                        float segAng = atan2f(dy, dx);
                        float dl = baseLen * (0.8f + 0.35f * breath);
                        float dt = baseThick * (0.8f + 0.35f * breath);
                        float a = 1.0f;
                        float fromStart = d - 6.0f, toHead = (totalL - headLen) - d;
                        if (fromStart < step) a *= fromStart / step;
                        if (toHead < step) a *= toHead / step;
                        if (a < 0.0f) a = 0.0f;
                        m_spriteRenderer->DrawSprite(m_whiteTexture, cx - dl / 2, cy - dt / 2, dl, dt, segAng,
                            XMFLOAT4(hue.x, hue.y, hue.z, 0.85f * a));
                        break;
                    }
                    acc += sl;
                }
            }
        }
}
