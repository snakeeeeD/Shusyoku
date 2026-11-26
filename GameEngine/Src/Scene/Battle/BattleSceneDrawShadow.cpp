#include "BattleScene.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "Player.h"
#include "Enemy.h"

// 落ち影（シャドウマップ）。光から見た深さの絵を先に描いておき、地面とマスがそれを見て暗くなる

namespace
{
    const XMFLOAT3 SHADOW_LIGHT_DIR = { -0.45f, -0.6f, -0.7f };  // 光の進む向き（前のにせ影と同じ向き。太陽を低めにして影を長く＝隣の影と重なる）
    const float    SHADOW_VIEW = 30.0f;                          // 光のカメラが写す広さ（盤面＋周りの装飾）
}

// 光から見た深さの絵を描く（装飾・敵・プレイヤー）
void BattleScene::DrawShadowPass(const BattleTheme& th)
{
    if (!m_useShadowMap || !m_renderer3D->HasShadowMap()) return;
    m_renderer3D->BeginShadowPass(SHADOW_LIGHT_DIR, XMFLOAT3(0, 0, 0), SHADOW_VIEW);
    for (auto& dc : m_decos)
        m_renderer3D->DrawShadowCaster(TextureManager::Get(th.tex[dc.type]), dc.x, -0.10f, dc.z,
            th.w[dc.type] * dc.scale, th.h[dc.type] * dc.scale);
    for (auto enemy : m_enemies) enemy->DrawShadowCaster(m_renderer3D);
    m_player->DrawShadowCaster(m_renderer3D);
    m_renderer3D->EndShadowPass();
}
