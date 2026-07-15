#include "BattleHighlighter.h"
#include "EnemyActionType.h"
#include "Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <map>

std::vector<std::pair<int, int>> BattleHighlighter::GetCandidates(
    int centerCol, int centerRow, RangeType rangeType, int range)
{
    std::vector<std::pair<int, int>> candidates;

    switch (rangeType)
    {
    case RangeType::Adjacent:
        candidates = {
            {centerCol,     centerRow - 1},
            {centerCol,     centerRow + 1},
            {centerCol - 1, centerRow    },
            {centerCol + 1, centerRow    },
        };
        break;
    case RangeType::Cross:
        for (int i = 1; i <= range; i++)
        {
            candidates.push_back({ centerCol,     centerRow - i });
            candidates.push_back({ centerCol,     centerRow + i });
            candidates.push_back({ centerCol - i, centerRow });
            candidates.push_back({ centerCol + i, centerRow });
        }
        break;
    case RangeType::Area:
        for (int dr = -range; dr <= range; dr++)
            for (int dc = -range; dc <= range; dc++)
                if (max(abs(dc), abs(dr)) <= range)
                    candidates.push_back({ centerCol + dc, centerRow + dr });
        break;
    case RangeType::Diamond:
        for (int dr = -range; dr <= range; dr++)
            for (int dc = -range; dc <= range; dc++)
                if (abs(dc) + abs(dr) <= range && (dc != 0 || dr != 0))
                    candidates.push_back({ centerCol + dc, centerRow + dr });
        break;
    case RangeType::Diagonal:
        for (int i = 1; i <= range; i++)
        {
            candidates.push_back({ centerCol + i, centerRow + i });
            candidates.push_back({ centerCol + i, centerRow - i });
            candidates.push_back({ centerCol - i, centerRow + i });
            candidates.push_back({ centerCol - i, centerRow - i });
        }
        break;
    case RangeType::DiagonalCross:
        for (int i = 1; i <= range; i++)
        {
            candidates.push_back({ centerCol,     centerRow - i });
            candidates.push_back({ centerCol,     centerRow + i });
            candidates.push_back({ centerCol - i, centerRow });
            candidates.push_back({ centerCol + i, centerRow });
            candidates.push_back({ centerCol + i, centerRow + i });
            candidates.push_back({ centerCol + i, centerRow - i });
            candidates.push_back({ centerCol - i, centerRow + i });
            candidates.push_back({ centerCol - i, centerRow - i });
        }
        break;
    case RangeType::None:
        candidates.push_back({ centerCol, centerRow });
        break;
    default:
        break;
    }

    return candidates;
}

bool BattleHighlighter::IsInEnemyRange(
    int col, int row,
    const EnemyAction* action,
    int enemyCol, int enemyRow,
    int rangeBonus)
{
    int dc = abs(col - enemyCol);
    int dr = abs(row - enemyRow);

    switch (action->rangeType)
    {
    case RangeType::Adjacent:    return (dc + dr) == 1;
    case RangeType::Cross:   return (dc == 0 || dr == 0) && (dc + dr) >= action->minRange && (dc + dr) <= action->range;
    case RangeType::Area:    return max(dc, dr) >= action->minRange && max(dc, dr) <= action->range;
    case RangeType::Diamond: return (dc + dr) >= action->minRange && (dc + dr) <= action->range;
    case RangeType::Diagonal:    return (dc == dr) && dc <= action->range;
    case RangeType::DiagonalCross:
        return ((dc == 0 || dr == 0) || (dc == dr)) && max(dc, dr) <= action->range;
    default:                     return (dc + dr) <= action->range;
    }
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
                if (!action || action->type != EnemyActionType::Attack) continue;
                if (enemy->IsInRange(col, row, action->range, action->rangeType, action->minRange))
                {
                    int finalDamage = action->value - player->GetBlock();
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
            // 射程内か確認
            int minDist = INT_MAX;
            for (auto enemy : enemies)
            {
                for (auto& [dc, dr] : enemy->GetGridShape())
                {
                    if (enemy->gridCol + dc == col && enemy->gridRow + dr == row)
                    {
                        for (auto& [dc2, dr2] : enemy->GetGridShape())
                        {
                            int dist = abs(centerCol - (enemy->gridCol + dc2))
                                + abs(centerRow - (enemy->gridRow + dr2));
                            minDist = min(minDist, dist);
                        }
                        break;
                    }
                }
            }
            if (minDist <= data->range)
                isHovered = true;
        }

        // 危険マス判定
        bool isDangerCell = false;
        for (auto enemy : enemies)
        {
            const EnemyAction* action = enemy->GetNextAction();
            if (!action || action->type != EnemyActionType::Attack) continue;

            if (enemy->IsInRange(col, row, action->range, action->rangeType, action->minRange))
            {
                int finalDamage = action->value - player->GetBlock();
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

//void BattleHighlighter::UpdateEnemyHighlight(
//    const std::vector<Enemy*>& enemies,
//    GridMap* gridMap,
//    const Player* player,
//    int playerCol, int playerRow,
//    float timer)
//{
//    ClearEnemyHighlight(gridMap);
//    for (auto e : enemies) e->color = XMFLOAT4(1, 1, 1, 1);   // 本体は塗らない
//
//    static const XMFLOAT4 palette[] = {
//        { 0.2f, 0.6f, 1.0f, 1.0f }, { 1.0f, 0.5f, 0.1f, 1.0f },
//        { 0.4f, 0.9f, 0.3f, 1.0f }, { 0.9f, 0.3f, 0.9f, 1.0f },
//        { 1.0f, 0.85f, 0.2f, 1.0f },
//    };
//    const int PN = 5;
//    auto lerp = [](const XMFLOAT4& a, const XMFLOAT4& b, float k) {
//        return XMFLOAT4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k,
//            a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k);
//        };
//
//    std::map<std::pair<int, int>, std::vector<std::pair<int, float>>> cellThreats;
//
//    for (int ei = 0; ei < (int)enemies.size(); ei++)
//    {
//        Enemy* enemy = enemies[ei];
//        const EnemyAction* action = enemy->GetNextAction();
//        if (!action || action->type != EnemyActionType::Attack) continue;
//
//        int rng = action->range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);
//        auto addCell = [&](int col, int row, int dist) {
//            if (col < 0 || col >= gridMap->GetCols() || row < 0 || row >= gridMap->GetRows()) return;
//            auto& c = gridMap->GetCell(col, row);
//            if (c.type == CellType::Enemy || c.type == CellType::Boss) return;
//            float fade = 1.0f - (float)(dist - 1) / (float)max(1, rng) * 0.5f;
//            fade = max(0.4f, min(1.0f, fade));
//            cellThreats[{col, row}].push_back({ ei, fade });
//            };
//
//        if (action->unavoidable) { cellThreats[{playerCol, playerRow}].push_back({ ei, 1.0f }); continue; }
//
//        if (action->rangeType == RangeType::Cone)
//        {
//            int adx = enemy->GetAimDx(), ady = enemy->GetAimDy();
//            for (int a = 1; a <= rng; a++)
//                for (int b = -(a - 1); b <= a - 1; b++)
//                    addCell(enemy->gridCol + a * adx + b * (-ady), enemy->gridRow + a * ady + b * adx, a);
//        }
//        else
//        {
//            auto candidates = GetCandidates(enemy->gridCol, enemy->gridRow, action->rangeType, rng);
//            for (auto& [col, row] : candidates)
//            {
//                int dc = abs(col - enemy->gridCol), dr = abs(row - enemy->gridRow);
//                int dist = (action->rangeType == RangeType::Area) ? max(dc, dr) : (dc + dr);
//                if (action->minRange > 0 && dist < action->minRange) continue;
//                addCell(col, row, dist);
//            }
//        }
//    }
//
//    m_enemyCycleTimer += 0.0048f;
//    int cyc = (int)(m_enemyCycleTimer / 6.28318f);
//    float pulse = 0.55f + 0.45f * sin(timer * 3.0f);   // 自分のマス用の点滅
//
//    std::map<std::pair<int, int>, XMFLOAT4> next;
//    for (auto& [pos, list] : cellThreats)
//    {
//        int pick = (list.size() == 1) ? 0 : (cyc % (int)list.size());
//        XMFLOAT4 base = palette[list[pick].first % PN];
//        float fade = list[pick].second;
//        XMFLOAT4 target(base.x * fade, base.y * fade, base.z * fade, 1.0f);
//
//        auto it = m_cellColors.find(pos);
//        XMFLOAT4 cur = (it == m_cellColors.end()) ? target : lerp(it->second, target, 0.005f);  // じんわり
//        next[pos] = cur;
//
//        XMFLOAT4 show = cur;
//        if (pos.first == playerCol && pos.second == playerRow)   // 自分が乗ってるマスは点滅強調
//            show = XMFLOAT4(cur.x * pulse, cur.y * pulse, cur.z * pulse, 1.0f);
//
//        gridMap->GetCell(pos.first, pos.second).gameObject.color = show;
//        m_enemyHighlightCells.push_back(pos);
//    }
//    m_cellColors = next;
//}

void BattleHighlighter::UpdateEnemyHighlight(
    const std::vector<Enemy*>& enemies, GridMap* gridMap, const Player* player,
    int playerCol, int playerRow, float timer)
{
    ClearEnemyHighlight(gridMap);
    for (auto e : enemies) e->color = XMFLOAT4(1, 1, 1, 1);

    for (auto enemy : enemies)
    {
        const EnemyAction* action = enemy->GetNextAction();
        if (!action || action->type != EnemyActionType::Attack) continue;
        int rng = action->range + enemy->GetBuffManager().GetBuffValue(BuffType::RangeUp);

        auto add = [&](int col, int row, int dist) {
            if (col < 0 || col >= gridMap->GetCols() || row < 0 || row >= gridMap->GetRows()) return;
            auto& cell = gridMap->GetCell(col, row);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) return;
            float a = 0.6f - (float)(dist - 1) / (float)max(1, rng) * 0.25f;
            a = max(0.2f, min(0.6f, a));
            cell.gameObject.color = XMFLOAT4(a, a, 0.0f, 1.0f);
            m_enemyHighlightCells.push_back({ col, row });
            };

        if (action->unavoidable)
        {
            auto& cell = gridMap->GetCell(playerCol, playerRow);
            cell.gameObject.color = XMFLOAT4(0.8f, 0.4f, 0.0f, 1.0f);
            m_enemyHighlightCells.push_back({ playerCol, playerRow });
            continue;
        }

        if (action->rangeType == RangeType::Cone)
        {
            int adx = enemy->GetAimDx(), ady = enemy->GetAimDy();
            for (int a = 1; a <= rng; a++)
                for (int b = -(a - 1); b <= a - 1; b++)
                    add(enemy->gridCol + a * adx + b * (-ady), enemy->gridRow + a * ady + b * adx, a);
        }
        else
        {
            auto cand = GetCandidates(enemy->gridCol, enemy->gridRow, action->rangeType, rng);
            for (auto& [col, row] : cand)
            {
                int dc = abs(col - enemy->gridCol), dr = abs(row - enemy->gridRow);
                int dist = (action->rangeType == RangeType::Area) ? max(dc, dr) : (dc + dr);
                if (action->minRange > 0 && dist < action->minRange) continue;
                add(col, row, dist);
            }
        }
    }
}