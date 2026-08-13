#include "BattleHighlighter.h"
#include "HighlightPalette.h"
#include "EnemyActionType.h"
#include "EnemyIntentVisual.h"
#include "Renderer3D.h"
#include "RangeShape.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <map>
#include <set>

std::vector<std::pair<int, int>> BattleHighlighter::GetCandidates(
    int centerCol, int centerRow, RangeType rangeType, int range, int aimDx, int aimDy)
{
    std::vector<std::pair<int, int>> out;
    int R = (range < 1) ? 1 : range;
    for (int dr = -R; dr <= R; dr++)
        for (int dc = -R; dc <= R; dc++)
            if (RangeShape::Contains(centerCol, centerRow, centerCol + dc, centerRow + dr,
                rangeType, range, 0, aimDx, aimDy))
                out.push_back({ centerCol + dc, centerRow + dr });
    return out;
}

void BattleHighlighter::ClearPlayerHighlight(GridMap* gridMap)
{
    for (auto& [col, row] : m_playerHighlightCells)
    {
        auto& cell = gridMap->GetCell(col, row);
        gridMap->SetCellType(col, row, cell.type);
    }
    m_playerHighlightCells.clear();
    m_outOfRangeCells.clear();
}

void BattleHighlighter::ClearEnemyHighlight(GridMap* gridMap)
{
    for (auto& [col, row] : m_enemyHighlightCells)
    {
        bool isPlayerHighlight = false;
        for (auto& [pc, pr] : m_playerHighlightCells)
            if (pc == col && pr == row) { isPlayerHighlight = true; break; }

        if (!isPlayerHighlight)
        {
            auto& cell = gridMap->GetCell(col, row);
            gridMap->SetCellType(col, row, cell.type);
        }
    }
    m_enemyHighlightCells.clear();
}

void BattleHighlighter::UpdatePlayerHighlight(
    int centerCol, int centerRow,
    const CardData* data,
    const std::vector<Enemy*>& enemies,
    GridMap* gridMap,
    const Player* player,
    float timer,
    std::pair<int, int> hoveredCell,
    Renderer3D* renderer3D,
    int screenWidth, int screenHeight,
    const RECT& cardArea,
    bool moveLocked)
{
    ClearPlayerHighlight(gridMap);
    if (!data) return;

    // Moveカード: BFS で到達可能マスのみハイライト
    if (data->type == CardType::Move)
    {
        int actualRange = player->GetMoveRange(data->range);
        float pulse = sin(timer * 2.0f);
        float hoverBrightness = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);

        bool pathMaxed = m_travelPath && (int)m_travelPath->size() >= actualRange;

        // BFS
        std::queue<std::pair<int, int>> bfsQueue;
        std::map<std::pair<int, int>, int> bfsDist;
        std::map<std::pair<int, int>, std::pair<int, int>> bfsParent;

        auto startPos = std::make_pair(centerCol, centerRow);
        bfsQueue.push(startPos);
        bfsDist[startPos] = 0;
        bfsParent[startPos] = { -1, -1 };

        const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };

        while (!bfsQueue.empty())
        {
            auto [col, row] = bfsQueue.front();
            bfsQueue.pop();

            if (bfsDist[{col, row}] >= actualRange)
                continue;

            for (int d = 0; d < 4; d++)
            {
                int nc = col + dirs[d][0];
                int nr = row + dirs[d][1];
                if (nc < 0 || nc >= gridMap->GetCols()
                    || nr < 0 || nr >= gridMap->GetRows())
                    continue;

                auto np = std::make_pair(nc, nr);
                if (bfsDist.count(np))
                    continue;

                auto& ncell = gridMap->GetCell(nc, nr);
                if (ncell.type != CellType::Empty)
                    continue;

                bfsDist[np] = bfsDist[{col, row}] + 1;
                bfsParent[np] = { col, row };
                bfsQueue.push(np);
            }
        }

        // 到達可能マスをハイライト（経路はマウスが通った道 = m_travelPath）
        for (auto& [pos, d] : bfsDist)
        {
            if (pos == startPos) continue;
            auto [col, row] = pos;

            m_playerHighlightCells.push_back({ col, row });

            if (moveLocked)
            {
                m_outOfRangeCells.push_back({ col, row });   // 移動不可：全マスに×
                continue;                                    // 緑ハイライトはしない
            }
            auto& cell = gridMap->GetCell(col, row);

            bool isHovered = (col == hoveredCell.first && row == hoveredCell.second);

            bool isOnPath = false;
            if (m_travelPath)
                for (auto& p : *m_travelPath)
                    if (p.first == col && p.second == row) { isOnPath = true; break; }

            bool isDangerCell = false;
            for (auto enemy : enemies)
            {
                const EnemyAction* action = enemy->GetNextAction();
                if (!action || !EnemyIntentVisual::IsHarmful(*action)) continue;
                // 必中は自分のマスだけ危険。それ以外は形状で判定
                bool threat = action->target.unavoidable
                    ? (col == centerCol && row == centerRow)
                    : enemy->IsThreateningCell(col, row, *action);

                if (threat)
                {
                    int finalDamage = EnemyIntentVisual::GetTotalDamage(*action, enemy->GetBuffManager())
                        - player->GetBlock();
                    if (finalDamage > 0) isDangerCell = true;
                    break;
                }
            }

            if (isHovered && !pathMaxed)
                cell.gameObject.color = HighlightPalette::Scale(HighlightPalette::MoveRange, hoverBrightness);
            else if (isOnPath)
                cell.gameObject.color = HighlightPalette::MovePath;
            else if (isDangerCell)
            {
                float blink = 0.6f + 0.4f * sin(timer * 2.0f);
                cell.gameObject.color = HighlightPalette::Scale(HighlightPalette::Danger, blink);
            }
            else
            {
                float brightness = 0.7f - (float)(d - 1) / (float)max(1, actualRange) * 0.3f;
                brightness = max(0.4f, min(0.7f, brightness));
                cell.gameObject.color = HighlightPalette::Scale(HighlightPalette::MoveRange, brightness);
            }
        }
        // 到達不可マス（範囲内だが経路なし）
        auto allCandidates = GetCandidates(centerCol, centerRow, data->rangeType, actualRange);
        for (auto& [col, row] : allCandidates)
        {
            if (col < 0 || col >= gridMap->GetCols()) continue;
            if (row < 0 || row >= gridMap->GetRows()) continue;
            if (col == centerCol && row == centerRow) continue;
            if (bfsDist.count({ col, row })) continue;

            auto& cell = gridMap->GetCell(col, row);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue;

            m_playerHighlightCells.push_back({ col, row });
            m_outOfRangeCells.push_back({ col, row });
            cell.gameObject.color = HighlightPalette::Unreachable;
        }
        return;
    }

    int aimDx = 0, aimDy = 0;
    if (data->rangeType == RangeType::Cone)
    {
        if (hoveredCell.first >= 0)
            RangeShape::CardinalAim(centerCol, centerRow,
                hoveredCell.first, hoveredCell.second, aimDx, aimDy);
        if (aimDx == 0 && aimDy == 0) aimDy = -1;   // 初期状態は上に扇
    }
    auto candidates = GetCandidates(centerCol, centerRow, data->rangeType, data->range, aimDx, aimDy);

    int actualRange = player->GetMoveRange(data->range);

    float pulse = sin(timer * 2.0f);
    float hoverBrightness = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);

    bool isAreaHovered = false;
    if (data->rangeType == RangeType::Area || data->rangeType == RangeType::Cone)
    {
        if (data->type == CardType::Attack)
        {
            isAreaHovered = true;
        }
        else
        {
            for (auto& [col, row] : candidates)
                if (col == hoveredCell.first && row == hoveredCell.second)
                {
                    isAreaHovered = true; break;
                }
        }
    }

    for (auto& [col, row] : candidates)
    {
        if (col < 0 || col >= gridMap->GetCols()) continue;
        if (row < 0 || row >= gridMap->GetRows()) continue;

        // 手札エリアと重なるかチェック
        float wx = (col - gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (row - gridMap->GetRows() / 2.0f) * 1.1f;
        XMVECTOR worldPos = XMVectorSet(wx, 0.0f, wz, 1.0f);
        XMVECTOR clipPos = XMVector4Transform(worldPos,
            renderer3D->GetViewMatrix() * renderer3D->GetProjectionMatrix());
        XMFLOAT4 clip;
        XMStoreFloat4(&clip, clipPos);

        float finalBrightness = hoverBrightness;

        auto& cell = gridMap->GetCell(col, row);

        if (data->mainEffect.type == CardEffectType::PlaceTrap &&
            cell.type != CellType::Empty) continue;   // 罠は空マスのみ

        if (data->type == CardType::Move &&
            (cell.type == CellType::Enemy || cell.type == CellType::Boss)) continue;

        m_playerHighlightCells.push_back({ col, row });

        // マウスが乗っているマスかどうか
        bool isHovered = (col == hoveredCell.first && row == hoveredCell.second);
        bool isOnHoveredLine = false;
        if (data->type == CardType::Attack && hoveredCell.first >= 0)
        {
            int hdx = hoveredCell.first - centerCol;
            int hdy = hoveredCell.second - centerRow;
            int cdx = col - centerCol;
            int cdy = row - centerRow;

            if (hdx == 0 && cdx == 0 && hdy != 0 && cdy != 0 && ((hdy > 0) == (cdy > 0)))
                isOnHoveredLine = true;
            if (hdy == 0 && cdy == 0 && hdx != 0 && cdx != 0 && ((hdx > 0) == (cdx > 0)))
                isOnHoveredLine = true;
        }
        bool isEnemy = (cell.type == CellType::Enemy || cell.type == CellType::Boss);

        // 攻撃カードは敵マスのみホバー有効
        if (data->type == CardType::Attack && isHovered && !isEnemy)
            isHovered = false;

        // 攻撃カードで射程内の敵マスは自動でホバー扱い
        if (data->type == CardType::Attack && isEnemy && !isHovered)
        {
            // 射程内か確認（表示形状と同じ判定）
            bool inShape = false;
            for (auto enemy : enemies)
            {
                bool isThisEnemy = false;
                for (auto& [dc, dr] : enemy->GetGridShape())
                    if (enemy->gridCol + dc == col && enemy->gridRow + dr == row) { isThisEnemy = true; break; }
                if (!isThisEnemy) continue;

                for (auto& [dc2, dr2] : enemy->GetGridShape())
                    if (RangeShape::Contains(centerCol, centerRow,
                        enemy->gridCol + dc2, enemy->gridRow + dr2,
                        data->rangeType, data->range, 0, aimDx, aimDy))
                    {
                        inShape = true; break;
                    }
                break;
            }
            if (inShape)
                isHovered = true;
        }

        // 危険マス判定
        bool isDangerCell = false;
        for (auto enemy : enemies)
        {
            const EnemyAction* action = enemy->GetNextAction();
            if (!action || !EnemyIntentVisual::IsHarmful(*action)) continue;

            // 必中は自分のマスだけ危険。それ以外は形状で判定
            bool threat = action->target.unavoidable
                ? (col == centerCol && row == centerRow)
                : enemy->IsThreateningCell(col, row, *action);

            if (threat)
            {
                int finalDamage = EnemyIntentVisual::GetTotalDamage(*action, enemy->GetBuffManager())
                    - player->GetBlock();
                if (finalDamage > 0) isDangerCell = true;
                break;
            }
        }

        // 色設定
        XMFLOAT4 base = HighlightPalette::ForCard(data->type);

        if (data->rangeType == RangeType::Area || data->rangeType == RangeType::Cone)
        {
            float brightness = isEnemy ? finalBrightness
                : (isAreaHovered ? finalBrightness : 0.6f);
            cell.gameObject.color = HighlightPalette::Scale(base, brightness);
        }
        else
        {
            int dc = abs(col - centerCol);
            int dr = abs(row - centerRow);
            float brightness = 0.7f - (float)(dc + dr - 1) / (float)max(1, data->range) * 0.3f;
            brightness = max(0.4f, min(0.7f, brightness));

            if (isHovered)
                cell.gameObject.color = HighlightPalette::Scale(base, finalBrightness);
            else if (data->type == CardType::Attack && isOnHoveredLine)
                cell.gameObject.color = isEnemy
                ? HighlightPalette::Scale(base, finalBrightness)
                : HighlightPalette::AttackLine;
            else if (data->type == CardType::Move && isDangerCell)
            {
                float blink = 0.6f + 0.4f * sin(timer * 2.0f);
                cell.gameObject.color = HighlightPalette::Scale(HighlightPalette::Danger, blink);
            }
            else
                cell.gameObject.color = HighlightPalette::Scale(base, brightness);
        }
    }
}

void BattleHighlighter::UpdateEnemyHighlight(
    const std::vector<Enemy*>& enemies, GridMap* gridMap, const Player* player,
    int playerCol, int playerRow, float timer, int decoyCol, int decoyRow)
{

    ClearEnemyHighlight(gridMap);
    for (auto e : enemies) e->color = HighlightPalette::EnemyNormal;

    std::map<std::pair<int, int>, std::pair<int, int>> cellOwner;  // マス -> (距離, 敵index)
    std::set<std::pair<int, int>> selCells;

    for (int ei = 0; ei < (int)enemies.size(); ei++)
    {
        Enemy* enemy = enemies[ei];
        const EnemyAction* action = enemy->GetNextAction();
        bool harmful = action && EnemyIntentVisual::IsHarmful(*action);

        // 足元マス：全ての敵に自分の色（薄め）
        XMFLOAT4 footCol = HighlightPalette::Scale(HighlightPalette::EnemyHue(ei), 0.5f);
        for (auto& [dc, dr] : enemy->GetGridShape())
        {
            int ec = enemy->gridCol + dc, er = enemy->gridRow + dr;
            if (ec >= 0 && ec < gridMap->GetCols() && er >= 0 && er < gridMap->GetRows())
                gridMap->GetCell(ec, er).gameObject.color = footCol;
        }

        if (!harmful) continue;

        // 足元マスをこの敵の色（薄め）で
        for (auto& [dc, dr] : enemy->GetGridShape())
        {
            int ec = enemy->gridCol + dc, er = enemy->gridRow + dr;
            if (ec >= 0 && ec < gridMap->GetCols() && er >= 0 && er < gridMap->GetRows())
                gridMap->GetCell(ec, er).gameObject.color =
                HighlightPalette::Scale(HighlightPalette::EnemyHue(ei), 0.5f);
        }

        auto mark = [&](int c, int r, int dist)
            {
                auto key = std::make_pair(c, r);
                auto it = cellOwner.find(key);

                if (ei == m_selectedEnemy)
                {
                    cellOwner[key] = { dist, ei };            // 選択中の敵は最優先で所有
                    selCells.insert(key);
                }
                else if (it == cellOwner.end()
                    || (it->second.second != m_selectedEnemy && dist < it->second.first))
                {
                    cellOwner[key] = { dist, ei };            // 選択敵の所有マスは奪わない／それ以外は近い敵優先
                }
            };

        if (action->target.unavoidable)
        {
            // 必中：デコイがあればデコイのマス、無ければプレイヤーのマス
            int tc = (decoyCol >= 0) ? decoyCol : playerCol;
            int tr = (decoyCol >= 0) ? decoyRow : playerRow;
            mark(tc, tr, 1);
            continue;
        }

        for (auto& [c, r] : enemy->GetThreatCells(*action, gridMap))
        {
            auto& cell = gridMap->GetCell(c, r);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue;
            int dist = abs(c - enemy->gridCol) + abs(r - enemy->gridRow);
            mark(c, r, dist);
        }
    }

    m_enemyCycleTimer += 0.005f;
    for (auto& [pos, info] : cellOwner)
    {
        int dist = info.first;
        int ei = info.second;
        float w = 0.5f + 0.5f * sin(m_enemyCycleTimer - dist * 0.8f);
        float br = 0.25f + 0.45f * w;
        const XMFLOAT4& hue = HighlightPalette::EnemyHue(ei);

        XMFLOAT4 c = selCells.count(pos)
            ? HighlightPalette::Scale(hue, br * 1.5f)   // 選択中の敵は明るく
            : HighlightPalette::Scale(hue, br * 0.7f);

        gridMap->GetCell(pos.first, pos.second).gameObject.color = c;
        m_enemyHighlightCells.push_back(pos);
    }
}