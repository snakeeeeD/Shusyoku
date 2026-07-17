#include "BattleHighlighter.h"
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
    int centerCol, int centerRow, RangeType rangeType, int range)
{
    std::vector<std::pair<int, int>> out;
    int R = (range < 1) ? 1 : range;
    for (int dr = -R; dr <= R; dr++)
        for (int dc = -R; dc <= R; dc++)
            if (RangeShape::Contains(centerCol, centerRow, centerCol + dc, centerRow + dr, rangeType, range))
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
    const RECT& cardArea)
{
    ClearPlayerHighlight(gridMap);
    if (!data) return;

    // Moveカード: BFS で到達可能マスのみハイライト
    if (data->type == CardType::Move)
    {
        int actualRange = player->GetBuffManager().GetFinalMoveRange(data->range);
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
            {
                cell.gameObject.color = XMFLOAT4(0.2f, hoverBrightness, 0.4f, 1.0f);
            }
            else if (isOnPath)
            {
                cell.gameObject.color = XMFLOAT4(0.3f, 0.85f, 0.5f, 1.0f);
            }
            else if (isDangerCell)
            {
                float blink = 0.6f + 0.4f * sin(timer * 2.0f);
                cell.gameObject.color = XMFLOAT4(blink, blink, 0.1f, 1.0f);
            }
            else
            {
                float brightness = 0.7f - (float)(d - 1) / (float)max(1, actualRange) * 0.3f;
                brightness = max(0.4f, min(0.7f, brightness));
                cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
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
            cell.gameObject.color = XMFLOAT4(0.25f, 0.25f, 0.25f, 1.0f);
        }
        return;
    }

    auto candidates = GetCandidates(centerCol, centerRow, data->rangeType, data->range);

    int actualRange = player->GetBuffManager().GetFinalMoveRange(data->range);

    float pulse = sin(timer * 2.0f);
    float hoverBrightness = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);

    bool isAreaHovered = false;
    if (data->rangeType == RangeType::Area)
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

        if (data->type == CardType::Move &&
            (cell.type == CellType::Enemy || cell.type == CellType::Boss)) continue;

        m_playerHighlightCells.push_back({ col, row });

        if (data->type == CardType::Move)
        {
            int dist = abs(col - centerCol) + abs(row - centerRow);
            if (dist > actualRange)
            {
                m_outOfRangeCells.push_back({ col, row });
                cell.gameObject.color = XMFLOAT4(0.3f, 0.3f, 0.3f, 1.0f);
                continue;
            }
        }

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
                        enemy->gridCol + dc2, enemy->gridRow + dr2, data->rangeType, data->range))
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
        if (data->rangeType == RangeType::Area)
        {
            if (isEnemy)
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(finalBrightness, 0.2f, 0.2f, 1.0f);
            }
            else
            {
                float brightness = isAreaHovered ? finalBrightness : 0.6f;
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(brightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
            }
        }
        else
        {
            int dc = abs(col - centerCol);
            int dr = abs(row - centerRow);
            float brightness = 0.7f - (float)(dc + dr - 1) / (float)max(1, data->range) * 0.3f;
            brightness = max(0.4f, min(0.7f, brightness));

            if (isHovered)
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(finalBrightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, finalBrightness, 0.4f, 1.0f);
            }
            else if (data->type == CardType::Attack && isOnHoveredLine)
            {
                if (isEnemy)
                    cell.gameObject.color = XMFLOAT4(finalBrightness, 0.2f, 0.2f, 1.0f);
                else
                    cell.gameObject.color = XMFLOAT4(0.35f, 0.15f, 0.15f, 1.0f);
            }
            else if (data->type == CardType::Move && isDangerCell)
            {
                float blink =  0.6f + 0.4f * sin(timer * 2.0f);
                cell.gameObject.color = XMFLOAT4(blink, blink, 0.1f, 1.0f);
            }
            else
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(brightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
                else if (data->type == CardType::Skill)
                    cell.gameObject.color = XMFLOAT4(0.2f, 0.2f, finalBrightness, 1.0f);
                else if (data->type == CardType::Power)
                    cell.gameObject.color = XMFLOAT4(finalBrightness, 0.2f, finalBrightness, 1.0f);
            }


        }
    }
}

void BattleHighlighter::UpdateEnemyHighlight(
    const std::vector<Enemy*>& enemies, GridMap* gridMap, const Player* player,
    int playerCol, int playerRow, float timer)
{
    ClearEnemyHighlight(gridMap);
    for (auto e : enemies) e->color = XMFLOAT4(1, 1, 1, 1);

    std::map<std::pair<int, int>, int> cellDist;
    std::set<std::pair<int, int>> selCells;
    for (int ei = 0; ei < (int)enemies.size(); ei++)
    {
        Enemy* enemy = enemies[ei];
        const EnemyAction* action = enemy->GetNextAction();
        if (!action || !EnemyIntentVisual::IsHarmful(*action)) continue;

        if (action->target.unavoidable)
        {
            // 必中：どこにいても当たるので、プレイヤーのマスだけ示す
            auto it = cellDist.find({ playerCol, playerRow });
            if (it == cellDist.end() || 1 < it->second) cellDist[{playerCol, playerRow}] = 1;
            if (ei == m_selectedEnemy) selCells.insert({ playerCol, playerRow });
            continue;
        }
        for (auto& [c, r] : enemy->GetThreatCells(*action, gridMap))
        {
            auto& cell = gridMap->GetCell(c, r);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue;

            int dist = abs(c - enemy->gridCol) + abs(r - enemy->gridRow);
            auto it = cellDist.find({ c, r });
            if (it == cellDist.end() || dist < it->second) cellDist[{c, r}] = dist;

            if (ei == m_selectedEnemy) selCells.insert({ c, r });
        }
    }

    m_enemyCycleTimer += 0.005f;
    for (auto& [pos, dist] : cellDist)
    {
        float w = 0.5f + 0.5f * sin(m_enemyCycleTimer - dist * 0.8f);
        float br = 0.25f + 0.45f * w;
        XMFLOAT4 c = selCells.count(pos)
            ? XMFLOAT4(br * 0.9, br * 0.9, 0.35f * br, 1.0f)   // 選択中：明るく脈動する黄
            : XMFLOAT4(br * 0.55f, br * 0.4f, 0.0f, 1.0f);         // 他：暗く沈む
        gridMap->GetCell(pos.first, pos.second).gameObject.color = c;
        m_enemyHighlightCells.push_back(pos);
    }

}