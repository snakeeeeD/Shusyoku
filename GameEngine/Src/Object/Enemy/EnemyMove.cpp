#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "TerrainDataBase.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Audio.h"
#include "EffectDataBase.h"   // 溶ける見た目の材質（fx_dissolve）
#include "CardExecutor.h"     // 床の効果（TriggerTerrain）
#include "RelicManager.h"     // 床の効果を受けないレリック
#include <algorithm>
#include <map>
#include <queue>

// 移動（近づく・離れる・突進・並ぶ・中央へ・予告どおり・移動予定の経路・軌跡の床）
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

bool Enemy::CanOccupy(GridMap* gridMap, int nc, int nr) const
{
    for (auto& [dc, dr] : GetGridShape())
    {
        int c = nc + dc, r = nr + dr;
        if (c < 0 || c >= gridMap->GetCols() || r < 0 || r >= gridMap->GetRows()) return false;
        if (gridMap->GetCell(c, r).type == CellType::Empty) continue;
        bool own = false;                              // 自分の現footprintは重なってOK
        for (auto& [odc, odr] : GetGridShape())
            if (c == gridCol + odc && r == gridRow + odr) { own = true; break; }
        if (!own) return false;                        // プレイヤー/他敵/壁はNG
    }
    return true;
}

void Enemy::MoveToward(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root))
        return;

    size_t pathStart = m_movePath.size();

    for (int step = 0; step < steps; step++)
    {
        int dc = playerCol - gridCol;
        int dr = playerRow - gridRow;

        if (abs(dc) + abs(dr) <= 1) break;         // 隣接したら詰め終わり

        std::vector<std::pair<int, int>> candidates;
        if (abs(dc) >= abs(dr))
        {
            candidates.push_back({ gridCol + (dc > 0 ? 1 : -1), gridRow });
            if (dr != 0)
                candidates.push_back({ gridCol, gridRow + (dr > 0 ? 1 : -1) });
        }
        else
        {
            candidates.push_back({ gridCol, gridRow + (dr > 0 ? 1 : -1) });
            if (dc != 0)
                candidates.push_back({ gridCol + (dc > 0 ? 1 : -1), gridRow });
        }

        bool moved = false;
        for (auto& [newCol, newRow] : candidates)
        {
            if (newCol < 0 || newCol >= gridMap->GetCols()) continue;
            if (newRow < 0 || newRow >= gridMap->GetRows()) continue;
            if (!CanOccupy(gridMap, newCol, newRow)) continue;

            ClearFootprint(gridMap);
            DropTrail(gridMap, gridCol, gridRow);      // ← 通ったマスに炎
            gridCol = newCol;
            gridRow = newRow;
            MarkFootprint(gridMap);
            m_movePath.push_back({ gridCol, gridRow });
            moved = true;
            break;
        }
        if (!moved) break;                         // 詰まったら終了
    }

    // 実際に動いた時だけスライド（0マス移動でStartMoveすると無反応な待ちが出るため）
    if (m_movePath.size() != pathStart)
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
}

void Enemy::DropTrail(GridMap* gridMap, int col, int row)
{
    if (!m_buffManager.HasBuff(BuffType::EmberTrail)) return;
    auto& cell = gridMap->GetCell(col, row);
    if (cell.tileEffect.active) return;          // 既存の床は上書きしない
    cell.tileEffect.active = true;
    cell.tileEffect.id = "fire";
    cell.tileEffect.enemyOwned = true;
    cell.tileEffect.value = 2;
    cell.tileEffect.hazardTurns = m_buffManager.GetBuffValue(BuffType::EmberTrail);   // 付与するバフの数字がターン数
    const TerrainDef* t = TerrainDataBase::Get("fire");
    if (t) cell.tileEffect.persistent = t->persistent;
}

void Enemy::MoveAway(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;

    const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };

    std::vector<std::pair<float, float>> pts;

    for (int step = 0; step < steps; step++)
    {
        int curDist = abs(playerCol - gridCol) + abs(playerRow - gridRow);

        // 4方向を「同じ現在地」から評価して、一番離れられる隣へ
        int bestCol = gridCol, bestRow = gridRow, bestDist = curDist;
        for (int d = 0; d < 4; d++)
        {
            int nc = gridCol + dirs[d][0];
            int nr = gridRow + dirs[d][1];
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            if (!CanOccupy(gridMap, nc, nr)) continue;
            int nd = abs(playerCol - nc) + abs(playerRow - nr);
            if (nd > bestDist) { bestDist = nd; bestCol = nc; bestRow = nr; }
        }

        if (bestDist == curDist) break;   // どこへ動いても離れられない＝行き止まり

        // ここで初めて実際に移動（footprintで多マスも安全に）
        ClearFootprint(gridMap);
        gridCol = bestCol; gridRow = bestRow;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });

        { float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz); pts.push_back({ wx, wz }); }   // 通過マス
    }


    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    if (!pts.empty()) StartWalk(pts, 0.1f);
}

bool Enemy::MoveDash(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return false;
    if (m_aimDx == 0 && m_aimDy == 0) return false;

    bool hit = false;
    for (int i = 0; i < steps; i++)
    {
        int nc = gridCol + m_aimDx, nr = gridRow + m_aimDy;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        for (auto& [dc, dr] : GetGridShape())               // 体のどこかがぶつかった＝ヒット
            if (nc + dc == playerCol && nr + dr == playerRow) hit = true;
        if (hit) break;
        if (!CanOccupy(gridMap, nc, nr)) break;

        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
        m_didDash = true;                        // ダッシュで実際に進んだ
    }

    return hit;
}

void Enemy::MoveAlongPlanned(GridMap* gridMap)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;

    size_t pathStart = m_movePath.size();

    for (auto& cell : m_plannedMovePath)
    {
        int nc = cell.first, nr = cell.second;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        if (!CanOccupy(gridMap, nc, nr)) break;  // 途中が埋まってたら手前で止まる
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
    }
    if (m_movePath.size() != pathStart)
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
    m_plannedMovePath.clear();   // 使い終わった矢印は消す（途中が埋まって手前で止まっても、矢印が残らないように）
}

void Enemy::AlignToTarget(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;
    size_t pathStart = m_movePath.size();
    for (int s = 0; s < steps; s++)
    {
        int dc = playerCol - gridCol, dr = playerRow - gridRow;
        if (dc == 0 || dr == 0) break;                    // 既に十字線上
        int mc = 0, mr = 0;
        if (abs(dc) <= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 小さい方の軸を詰める＝距離は保つ
        else                    mr = (dr > 0) ? 1 : -1;
        int nc = gridCol + mc, nr = gridRow + mr;
        if (!CanOccupy(gridMap, nc, nr)) break;           // 障害物で停止
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });        // 通過罠判定用
    }
    if (m_movePath.size() != pathStart)
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
}

void Enemy::MoveToCenter(GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;
    int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
    size_t pathStart = m_movePath.size();
    for (int s = 0; s < steps; s++)
    {
        int dc = cc - gridCol, dr = cr - gridRow;
        if (dc == 0 && dr == 0) break;                    // 中央到達
        int mc = 0, mr = 0;
        if (abs(dc) >= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 大きい方の軸から詰める
        else                    mr = (dr > 0) ? 1 : -1;
        int nc = gridCol + mc, nr = gridRow + mr;
        if (!CanOccupy(gridMap, nc, nr)) break;
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
    }
    if (m_movePath.size() != pathStart)
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
}

// 目標へ1マス貪欲移動（隣接で停止）。動けなければfalse
static bool StepToward(int& c, int& r, int tc, int tr, GridMap* g, std::vector<std::pair<int, int>>& path)
{
    int dc = tc - c, dr = tr - r;
    if (abs(dc) + abs(dr) <= 1) return false;
    std::pair<int, int> cand[2]; int n = 0;
    if (abs(dc) >= abs(dr)) { cand[n++] = { c + (dc > 0 ? 1 : -1), r }; if (dr != 0) cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; }
    else { cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; if (dc != 0) cand[n++] = { c + (dc > 0 ? 1 : -1), r }; }
    for (int k = 0; k < n; k++)
    {
        int mx = cand[k].first, my = cand[k].second;
        if (mx < 0 || mx >= g->GetCols() || my < 0 || my >= g->GetRows()) continue;
        if (g->GetCell(mx, my).type != CellType::Empty) continue;
        c = mx; r = my; path.push_back({ c, r }); return true;
    }
    return false;
}

std::vector<std::pair<int, int>> Enemy::PlannedMovePath(int targetCol, int targetRow, GridMap* gridMap) const
{
    std::vector<std::pair<int, int>> path;
    const EnemyAction* a = GetNextAction();
    if (!a) return path;
    if (HasLunge(*a)) return m_lungePlan;   // 噛みつきは予告の経路そのもの
    const TargetSpec& tg = a->target;

    // 攻撃の接近（Dash / Toward）
    if (!tg.unavoidable && tg.moveRange > 0)
    {
        if (tg.approach == ApproachType::Dash)
        {
            if (m_aimDx == 0 && m_aimDy == 0) return path;
            int c = gridCol, r = gridRow;
            for (int i = 0; i < tg.moveRange; i++)
            {
                int nc = c + m_aimDx, nr = r + m_aimDy;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
                if (!CanOccupy(gridMap, nc, nr)) break;             // 形全体で判定（プレイヤーのマスも含む）
                c = nc; r = nr; path.push_back({ c, r });
            }
            return path;
        }
        if (tg.approach == ApproachType::Toward)
        {
            if (IsThreateningCell(targetCol, targetRow, *a)) return path;   // 既に当たるなら接近しない（実行側と一致）
            int c = gridCol, r = gridRow;
            for (int s = 0; s < tg.moveRange; s++) if (!StepToward(c, r, targetCol, targetRow, gridMap, path)) break;
            return path;
        }
        if (tg.approach == ApproachType::Align)
        {
            int c = gridCol, r = gridRow;
            for (int s = 0; s < tg.moveRange; s++)
            {
                int dc = targetCol - c, dr = targetRow - r;
                if (dc == 0 || dr == 0) break;                    // 十字線上＝整列済み
                int mc = 0, mr = 0;
                if (abs(dc) <= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 小さい方の軸を詰める（距離は保つ）
                else                    mr = (dr > 0) ? 1 : -1;
                int nc = c + mc, nr = r + mr;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
                if (gridMap->GetCell(nc, nr).type != CellType::Empty) break;
                c = nc; r = nr; path.push_back({ c, r });
            }
            return path;
        }
    }

    // 純移動効果（MoveToward / MoveAway）
    for (auto& e : a->effects)
    {
        if (e.kind == EffectKind::MoveToward)
        {
            int c = gridCol, r = gridRow;
            for (int s = 0; s < e.value; s++) if (!StepToward(c, r, targetCol, targetRow, gridMap, path)) break;
            return path;
        }
        if (e.kind == EffectKind::MoveAway)
        {
            const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
            int c = gridCol, r = gridRow;
            for (int s = 0; s < e.value; s++)
            {
                int curD = abs(targetCol - c) + abs(targetRow - r);
                int bc = c, br = r, bd = curD;
                for (auto& d : dirs)
                {
                    int nc = c + d[0], nr = r + d[1];
                    if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
                    if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
                    int nd = abs(targetCol - nc) + abs(targetRow - nr);
                    if (nd > bd) { bd = nd; bc = nc; br = nr; }
                }
                if (bd == curD) break;
                c = bc; r = br; path.push_back({ c, r });
            }
            return path;
        }
        if (e.kind == EffectKind::Coil)
        {
            for (int k = 0; k < m_neckLen; k++)             // 首を伸ばしている時は、まず首を戻す道を通る
                path.push_back(m_bodyCells[m_bodyCells.size() - 1 - k]);
            if (m_coilReached || m_buffManager.HasBuff(BuffType::Root)) return path;   // 中央到達後・拘束中は巻かない
            for (int s = 1; s <= e.value + m_coilBonus; s++)   // 脱皮で速くなった分も含める
            {
                int idx = m_spiralIdx + s;
                if (idx >= (int)m_spiral.size()) break;
                path.push_back(m_spiral[idx]);
            }
            return path;
        }
    }
    return path;
}

// 行動の前の移動：近づく（予告の矢印どおり）・突進・縦横に並ぶ（必中の行動は動かない）
void Enemy::ApproachTarget(ActionRun& r)
{
    if (r.tg.unavoidable) return;
    switch (r.tg.approach)
    {
    case ApproachType::Toward:
        if (!r.hitTarget)
        {
            if (!m_plannedMovePath.empty()) MoveAlongPlanned(r.gridMap);          // 矢印どおりに動く
            else                            MoveToward(r.mtC, r.mtR, r.gridMap, r.tg.moveRange);  // 保険
        }
        break;
    case ApproachType::Dash:
    {
        bool dh = MoveDash(r.mtC, r.mtR, r.gridMap, r.tg.moveRange);
        r.hitTarget = r.hitTarget || dh;
        break;
    }
    case ApproachType::Align:
        if (!r.hitTarget) AlignToTarget(r.mtC, r.mtR, r.gridMap, r.tg.moveRange);
        break;
    default: break;
    }
}
