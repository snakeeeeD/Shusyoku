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

// 盤面の描画（狙ったマスを浮かせる・地面・マス・地面の演出）

static constexpr float RAISE_H = 0.12f;   // 狙ったマスが浮く高さ（上げすぎると奥のマスに食い込んで見える）

// 狙ったマスを少し浮かせる（なめらかに）。上に立つキャラも合わせる
void BattleScene::UpdateCellHeights()
{
    std::set<std::pair<int, int>> raised = CollectRaisedCells();
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
        {
            auto& cell = m_gridMap->GetCell(col, row);
            float target = raised.count({ col, row }) ? RAISE_H : 0.0f;
            cell.gameObject.worldY += (target - cell.gameObject.worldY) * 0.2f;   // 補間
        }

    SyncUnitHeights();
}

// 狙っているマスを集める（浮かせるマス）：選んでいるカードの種類ごと（攻撃は当たる敵・移動・扇・起爆系・罠を置く・スキル）
std::set<std::pair<int, int>> BattleScene::CollectRaisedCells()
{
    std::set<std::pair<int, int>> raised;
    if (m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size())
    {
        CardData dThrow = *m_hand.GetCards()[m_selectedCardIndex]->GetData();
        ApplyKnifeThrow(dThrow, m_player);
        const CardData* d = &dThrow;
        auto raiseTrapsInRange = [&]() {   // 範囲内の自分の罠（起爆系3種で共通。敵床は光らせない）
            for (auto& cc : BattleHighlighter::GetCandidates(m_playerCol, m_playerRow, d->rangeType, d->range))
            {
                if (cc.first < 0 || cc.first >= m_gridMap->GetCols() ||
                    cc.second < 0 || cc.second >= m_gridMap->GetRows()) continue;
                auto& hc = m_gridMap->GetCell(cc.first, cc.second);
                if (hc.tileEffect.active && !hc.tileEffect.enemyOwned)
                    raised.insert(cc);
            }
        };
        // 敵1体・アタックは、ホバーしていなくても敵マスを狙い先にする（ホバー時と同じ浮き演出）
        std::pair<int, int> hov = m_hoveredCell;
        // 攻撃は「今離したら当たる敵」の占有マスを浮かせる（使用時と同じ判定）
        if (const Enemy* t = AttackTarget())
        {
            if (!CellInCardRange(*d, hov.first, hov.second))
                hov = { t->gridCol, t->gridRow };   // 射程内マス以外は狙う敵へ
            for (auto& c : t->OccupiedCells()) raised.insert(c);
        }
        if (d->type == CardType::Move)
        {
            for (auto& p : m_movePath) raised.insert(p);
        }
        else if (d->rangeType == RangeType::Cone)
        {
            int range = d->range;
            if (m_player->GetBuffManager().HasBuff(BuffType::Reposition))
                range += m_player->GetBuffManager().GetBuffValue(BuffType::Reposition);

            int aimDx = 0, aimDy = 0;
            if (hov.first >= 0)
                RangeShape::CardinalAim(m_playerCol, m_playerRow,
                    hov.first, hov.second, aimDx, aimDy);
            if (aimDx == 0 && aimDy == 0) aimDy = -1;   // 初期は上向き

            for (auto& c : BattleHighlighter::GetCandidates(
                m_playerCol, m_playerRow, d->rangeType, range, aimDx, aimDy))
                raised.insert(c);
        }
        else if (d->mainEffect.type == CardEffectType::Detonate)
        {
            raiseTrapsInRange();
        }
        else if (d->mainEffect.type == CardEffectType::DetonateAt)
        {
            // 置いてある罠（範囲内）をハイライト
            raiseTrapsInRange();

            // 3x3プレビュー
            if (m_hoveredCell.first >= 0 &&
                CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second) &&
                m_gridMap->GetCell(m_hoveredCell.first, m_hoveredCell.second).tileEffect.active)
            {
                for (int dr = -1; dr <= 1; dr++)
                    for (int dc = -1; dc <= 1; dc++)
                        raised.insert({ m_hoveredCell.first + dc, m_hoveredCell.second + dr });
            }
        }
        else if (d->mainEffect.type == CardEffectType::DetonateChain)
        {
            // 置いてある罠（範囲内）をハイライト
            raiseTrapsInRange();

            // 連鎖の起点プレビュー（既存のまま）
            if (m_hoveredCell.first >= 0 &&
                CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second) &&
                m_gridMap->GetCell(m_hoveredCell.first, m_hoveredCell.second).tileEffect.active)
            {
                raised.insert({ m_hoveredCell.first, m_hoveredCell.second });
            }
        }
        else if (d->mainEffect.type == CardEffectType::PlaceTrapArea)
        {
            for (auto& cc : BattleHighlighter::GetCandidates(
                m_playerCol, m_playerRow, d->rangeType, d->range))
                raised.insert(cc);
        }
        else if ((d->type == CardType::Skill || d->type == CardType::Power)
            && !PicksCell(*d)
            && d->mainEffect.type != CardEffectType::Detonate)
        {
            raised.insert({ m_playerCol, m_playerRow });
            m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.color
                = HighlightPalette::ForCard(d->type);   // 危険色よりスキル色を優先
        }
        else if (hov.first >= 0)
        {
            if (CellInCardRange(*d, hov.first, hov.second))   // 射程の判定は使用時と同じ（足元も上がる）
                raised.insert({ hov.first, hov.second });
        }
        // 起爆系：足元の罠も、ほかの罠と同じように上げる
        if (d->mainEffect.type == CardEffectType::Detonate
            || d->mainEffect.type == CardEffectType::DetonateAt
            || d->mainEffect.type == CardEffectType::DetonateChain)
        {
            auto& own = m_gridMap->GetCell(m_playerCol, m_playerRow);
            if (own.tileEffect.active && !own.tileEffect.enemyOwned)
                raised.insert({ m_playerCol, m_playerRow });
        }

        // 全敵効果カード：全敵マスも範囲としてハイライト
        if (d->allEnemyEffect.hasEffect)
            for (auto en : m_enemies)
                if (en && en->GetHp() > 0)
                    for (auto& [dc, dr] : en->GetGridShape())
                        raised.insert({ en->gridCol + dc, en->gridRow + dr });
    }
    return raised;
}

// その上に立つキャラの高さ（敵は占有マスの一番高い浮きに合わせる）
void BattleScene::SyncUnitHeights()
{
    for (auto enemy : m_enemies)
    {
        auto safeY = [&](int c, int r) -> float {
            if (c < 0 || c >= m_gridMap->GetCols() || r < 0 || r >= m_gridMap->GetRows()) return 0.0f;
            return m_gridMap->GetCell(c, r).gameObject.worldY;
            };
        float maxY = safeY(enemy->gridCol, enemy->gridRow);
        if (enemy->IsSnake())
            for (auto& b : enemy->GetBodyCells())
                maxY = max(maxY, safeY(b.first, b.second));
        else
            for (auto& [dc, dr] : enemy->GetGridShape())
                maxY = max(maxY, safeY(enemy->gridCol + dc, enemy->gridRow + dr));
        enemy->worldY = 0.05f + maxY;
    }

    m_player->worldY = m_gridMap->GetCell(m_playerCol, m_playerRow).gameObject.worldY;
}

// 地面のタイルと背景の装飾（影つき）
void BattleScene::DrawGround(const BattleTheme& th)
{
    // === 接地用の地面（見下ろし草地をタイル状に敷く） ===
    m_renderer3D->SetShadowReceive(m_useShadowMap);   // 地面のタイルは落ち影を受ける
    {
        const float TILE = 6.0f;   // 1枚のワールドサイズ（テクスチャ1回分）
        const int   N = 6;         // 中心から±N枚（(2N+1)^2枚）
        for (int gz = -N; gz <= N; gz++)
            for (int gx = -N; gx <= N; gx++)
                m_renderer3D->DrawTile(TextureManager::Get(th.ground),
                    gx * TILE, gz * TILE, TILE * 1.02f, XMFLOAT4(th.bright, th.bright, th.bright, 1), -0.06f);
    }
    m_renderer3D->SetShadowReceive(false);

    // 背景装飾（透明quadがdepthを書いて四角化するのを防ぐため depth-write OFF）
    m_renderer3D->SetDepthWrite(false);
    // 影を先に全部（後から描くと、奥の装飾の本体に手前の影が乗るため）
    for (auto& dc : m_decos)
    {
        if (m_useShadowMap && m_renderer3D->HasShadowMap()) break;   // 落ち影を使う時は、にせ影は描かない
        const char* tex = th.tex[dc.type];
        float w = th.w[dc.type] * dc.scale;
        float h = th.h[dc.type] * dc.scale;
        // 光を浴びて斜めに伸びる影（本体シルエットをシアー投影。足元固定・頭が倒れる）
        float lenX = -h * 0.45f, lenZ = -h * 0.7f;  // 斜めの向き×長さ
        float dl = sqrtf(lenX * lenX + lenZ * lenZ);
        float tuck = -0.1f;                          // 固定の食い込み量（全種共通・ワールド単位）
        float bx = dc.x ;
        float bz = dc.z - (lenZ / dl) * tuck;
        m_renderer3D->DrawShadow(TextureManager::Get(tex),
            bx, bz, w, lenX, lenZ, XMFLOAT4(0.0f, 0.0f, 0.0f, 0.30f));
    }
    // 本体：m_decos は Init で奥→手前の順に並べてあるので、画面の下（手前）の物ほど上に描かれる
    for (auto& dc : m_decos)
    {
        const char* tex = th.tex[dc.type];
        float w = th.w[dc.type] * dc.scale;
        float h = th.h[dc.type] * dc.scale;
        m_renderer3D->DrawBillboard(TextureManager::Get(tex),
            dc.x, -0.10f, dc.z, w, h, 0.0f, XMFLOAT4(th.bright, th.bright, th.bright, 1));  // y=中心ではなく足元(地面)
    }
}

// マス（浮いたマスは暗い台を残して厚みに見せる）
void BattleScene::DrawCells(const BattleTheme& th)
{
    // マスの地面は深度を書かない：手前のマスが浮いても、奥に立つ敵やプレイヤーを隠さない（めり込み防止）。
    // 奥の行から順に描くので、マス同士の重なりは描く順で正しくなる（depth-write は装飾から OFF のまま）

    // 明るくするのは「何も表示していない空マス」だけ（範囲・ハイライトは素の色のまま）
    {
        int cols = m_gridMap->GetCols(), rows = m_gridMap->GetRows();
        std::vector<char> shown(cols * rows, 0);
        for (auto& mk : m_highlighter.GetThreatMarks())
            if (mk.col >= 0 && mk.col < cols && mk.row >= 0 && mk.row < rows)
                shown[mk.row * cols + mk.col] = 1;
        for (auto& p : m_highlighter.GetPlayerHighlightCells())
            if (p.first >= 0 && p.first < cols && p.second >= 0 && p.second < rows)
                shown[p.second * cols + p.first] = 1;

        m_renderer3D->SetShadowReceive(m_useShadowMap);   // マスも落ち影を受ける（キャラの影はほぼマスの上に落ちる）
        for (int row = 0; row < rows; row++)
            for (int col = 0; col < cols; col++)
            {
                float x = (col - cols / 2.0f) * 1.1f;
                float z = (row - rows / 2.0f) * 1.1f;
                auto& cell = m_gridMap->GetCell(col, row);
                float t = cell.gameObject.worldY / 0.10f;
                float size = 1.0f + 0.02f * t;

                XMFLOAT4 cc = cell.gameObject.color;
                if (cell.type == CellType::Empty && !shown[row * cols + col])
                    cc = th.cellCol;                                    // 空マスは層の地面に合った色
                const char* ctex = th.cellTex;                          // 石板は数種をマスごとに使い分け（模様の繰り返しを消す）
                char vname[24];
                if (th.cellVariants > 1) { sprintf_s(vname, "%s_%d", th.cellTex, (col * 7 + row * 3) % th.cellVariants); ctex = vname; }
                if (cell.gameObject.worldY > 0.01f)   // 浮いたマス：元の位置に暗い台を残して、厚み（側面）に見せる
                    m_renderer3D->DrawTile(m_whiteTexture, x, z, size,
                        XMFLOAT4(cc.x * 0.4f, cc.y * 0.4f, cc.z * 0.4f, 1.0f), 0.005f);
                m_renderer3D->DrawTile(TextureManager::Get(ctex), x, z, size, cc, cell.gameObject.worldY);
            }
        m_renderer3D->SetShadowReceive(false);
    }
}

// 地面の順番のエフェクト（衝撃波の輪、壁の奥半分など）。深度を見ずに地面の上へ重ねる。キャラはこの後に描くので、キャラの後ろに見える
void BattleScene::DrawGroundEffects()
{
    m_renderer3D->SetDepthEnabled(false);
    EffectManager::Draw(m_renderer3D, EffectLayer::Ground);
    m_renderer3D->SetDepthWrite(false);   // 罠を描いたときと同じ（深度は見るが書かない）に戻す
}
