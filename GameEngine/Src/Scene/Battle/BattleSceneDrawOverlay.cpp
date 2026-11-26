#include "CardEffect.h"
#include "BattleScene.h"
#include "CardTooltip.h"
#include "TextureLoader.h"
#include "EffectManager.h"
#include "RelicManager.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// 上に重ねる描画（エフェクト・必中のクロスヘア・移動先・狙ったマスの枠・覚醒演出）

// 上に重ねるもの（エフェクト・必中のクロスヘア・移動先のゴースト・狙ったマスの枠）
void BattleScene::DrawOverlays()
{
    m_renderer3D->SetDepthEnabled(false);
    EffectManager::Draw(m_renderer3D, EffectLayer::Overlay);
    DrawUnavoidableCrosshair();
    m_renderer3D->SetDepthEnabled(true);

    DrawPathGhost();
    DrawRaisedCellCorners();
}

// 必中攻撃の対象にクロスヘア（デコイ優先）。対象の実座標に合わせて画面端でもズレないように
void BattleScene::DrawUnavoidableCrosshair()
{
    bool showCross = false;
    for (auto enemy : m_enemies)
    {
        const EnemyAction* act = enemy->GetNextAction();
        if (act && act->target.unavoidable && !EnemyIntentVisual::Hidden()) { showCross = true; break; }
    }
    if (showCross)
    {
        float cx, cy, cz;
        if (m_decoyCol >= 0)
        {
            cx = CellWX(m_decoyCol);   // デコイはマス固定
            cz = CellWZ(m_decoyRow);
            cy = 0.4f;
        }
        else
        {
            cx = m_player->worldX;              // プレイヤーは実際の描画位置に合わせる
            cz = m_player->worldZ;
            cy = m_player->worldY + 0.35f;
        }

        m_renderer3D->SetDepthEnabled(false);
        float pulse = 0.85f + 0.15f * sinf(m_highlightTimer * 6.0f);
        m_renderer3D->DrawBillboard(TextureManager::Get("crosshair"),
            cx, cy, cz, 0.9f * pulse, 0.9f * pulse, 0.0f, XMFLOAT4(1.0f, 0.3f, 0.2f, 0.95f));
        m_renderer3D->SetDepthEnabled(true);
    }
}

// 移動経路の終点に半透明のプレイヤー
void BattleScene::DrawPathGhost()
{
    if (!m_pathBuilding || m_movePath.empty()) return;
    int gc = m_movePath.back().first;
    int gr = m_movePath.back().second;
    float gx = CellWX(gc);
    float gz = CellWZ(gr);
    float gy = m_player->worldY + m_gridMap->GetCell(gc, gr).gameObject.worldY;   // ← マスの浮きぶん
    m_renderer3D->DrawBillboard(
        TextureManager::Get("player"),
        gx, gy, gz,
        m_player->width, m_player->height, 0.0f,
        XMFLOAT4(0.5f, 0.7f, 1.0f, 0.3f));
}

// 狙ったマス（浮いているマス）の四隅にL字枠
void BattleScene::DrawRaisedCellCorners()
{
    XMMATRIX vp = m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix();
    auto toScreen = [&](float wx, float wy, float wz, float& ox, float& oy) -> bool {
        XMVECTOR w = XMVectorSet(wx, wy, wz, 1.0f);
        XMVECTOR c = XMVector4Transform(w, vp);
        XMFLOAT4 cl; XMStoreFloat4(&cl, c);
        if (cl.w <= 0.0f) return false;
        ox = (cl.x / cl.w + 1.0f) * 0.5f * m_screenWidth;
        oy = (1.0f - cl.y / cl.w) * 0.5f * m_screenHeight;
        return true;
        };
    auto drawLine = [&](float ax, float ay, float bx, float by, float th, const XMFLOAT4& c) {
        float mx = (ax + bx) * 0.5f, my = (ay + by) * 0.5f;
        float dx = bx - ax, dy = by - ay;
        float len = sqrtf(dx * dx + dy * dy);
        float ang = atan2f(dy, dx);
        m_spriteRenderer->DrawSprite(m_whiteTexture, mx - len * 0.5f, my - th * 0.5f, len, th, ang, c);
        };

    m_spriteRenderer->Begin();
    const float h = 0.5f;       // セル半径（枠の位置、要調整）
    const float t = 0.32f;      // 角の長さ比
    const XMFLOAT4 col(1.0f, 0.9f, 0.3f, 0.95f);
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int cc = 0; cc < m_gridMap->GetCols(); cc++)
        {
            auto& cell = m_gridMap->GetCell(cc, row);
            if (cell.gameObject.worldY < 0.05f) continue;   // 上がってるマスだけ
            float wx = CellWX(cc);
            float wz = CellWZ(row);
            float wy = cell.gameObject.worldY + 0.02f;
            float sx[4], sy[4];
            bool ok = toScreen(wx - h, wy, wz - h, sx[0], sy[0])
                && toScreen(wx + h, wy, wz - h, sx[1], sy[1])
                && toScreen(wx + h, wy, wz + h, sx[2], sy[2])
                && toScreen(wx - h, wy, wz + h, sx[3], sy[3]);
            if (!ok) continue;
            for (int k = 0; k < 4; k++)
            {
                int n = (k + 1) % 4, p = (k + 3) % 4;
                drawLine(sx[k], sy[k], sx[k] + (sx[n] - sx[k]) * t, sy[k] + (sy[n] - sy[k]) * t, 3.0f, col);
                drawLine(sx[k], sy[k], sx[k] + (sx[p] - sx[k]) * t, sy[k] + (sy[p] - sy[k]) * t, 3.0f, col);
            }
        }
    m_spriteRenderer->End();
}

// 3層ボスの覚醒演出：集中線＋ブラー、ボスだけくっきり、広がる円
void BattleScene::DrawAwakenCinematic()
{
    // 中心＝ボスの画面位置（ブラー前に計算：カメラは同じ）
    float cx = m_screenWidth * 0.5f, cy = m_screenHeight * 0.5f;
    if (m_awakenBoss)
    {
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, XMVector4Transform(
            XMVectorSet(m_awakenBoss->worldX, 0.0f, m_awakenBoss->worldZ, 1.0f),
            m_renderer3D->GetViewMatrix() * m_renderer3D->GetProjectionMatrix()));
        if (clip.w > 0.0f)
        {
            cx = (clip.x / clip.w + 1.0f) * 0.5f * m_screenWidth;
            cy = (1.0f - clip.y / clip.w) * 0.5f * m_screenHeight;
        }
    }
    float maxR = 0.0f;
    maxR = max(maxR, hypotf(cx, cy));
    maxR = max(maxR, hypotf(m_screenWidth - cx, cy));
    maxR = max(maxR, hypotf(cx, m_screenHeight - cy));
    maxR = max(maxR, hypotf(m_screenWidth - cx, m_screenHeight - cy));
    maxR *= 1.1f;
    float ph = m_awakenCinematic;

    // 集中線をオフスクリーン（ブラー前）に描く → ブラー対象に
    auto white = TextureManager::Get("white");
    const int LINES = 84;
    m_spriteRenderer->Begin();
    for (int i = 0; i < LINES; i++)
    {
        float ang = (float)i / LINES * 6.2831853f + sinf(ph * 30.0f + i * 12.9898f) * 0.02f;
        float Ri = maxR * (0.34f + 0.06f * sinf(i * 7.13f));
        float Ro = maxR * 1.4f;
        float len = Ro - Ri;
        float mx = cx + cosf(ang) * (Ri + Ro) * 0.5f;
        float my = cy + sinf(ang) * (Ri + Ro) * 0.5f;
        float th = 2.0f + 5.0f * (0.5f + 0.5f * sinf(i * 3.7f));
        m_spriteRenderer->DrawSprite(white, mx - len * 0.5f, my - th * 0.5f, len, th,
            ang, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f));
    }
    m_spriteRenderer->End();

    // ブラー合成（シーン＋集中線をまとめてぼかす）
    m_renderer3D->EndOffscreenBlur(2.0f);

    // ボスだけシャープ再描画
    if (m_awakenBoss)
    {
        m_renderer3D->Begin();
        m_renderer3D->SetDepthEnabled(false);
        m_awakenBoss->Draw3D(m_renderer3D);
        m_renderer3D->SetDepthEnabled(true);
        m_renderer3D->End();
    }

    // 円はシャープ（ブラー後）
    m_spriteRenderer->Begin();
    auto ring = TextureManager::Get("ui_hitring");
    for (float a : m_awakenRings)
    {
        float t = a / 1.1f;
        float R = t * maxR;
        float alpha = (1.0f - t) * 0.85f;
        m_spriteRenderer->DrawSprite(ring, cx - R, cy - R, R * 2.0f, R * 2.0f,
            0.0f, XMFLOAT4(1.0f, 0.35f, 0.2f, alpha));
    }
    m_spriteRenderer->End();
}
