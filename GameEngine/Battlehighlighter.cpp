#include "BattleHighlighter.h"
#include "EnemyActionType.h"
#include <algorithm>
#include <cmath>

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
    int enemyCol, int enemyRow)
{
    int dc = abs(col - enemyCol);
    int dr = abs(row - enemyRow);

    switch (action->rangeType)
    {
    case RangeType::Adjacent:    return (dc + dr) == 1;
    case RangeType::Cross:       return (dc == 0 || dr == 0) && (dc + dr) <= action->range;
    case RangeType::Area:        return max(dc, dr) <= action->range;
    case RangeType::Diamond:     return (dc + dr) <= action->range;
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
    std::pair<int, int> hoveredCell)
{
    ClearPlayerHighlight(gridMap);
    if (!data) return;

    auto candidates = GetCandidates(centerCol, centerRow, data->rangeType, data->range);

    float pulse = sin(timer * 2.0f);
    float hoverBrightness = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);

    bool isAreaHovered = false;
    if (data->rangeType == RangeType::Area)
    {
        for (auto& [col, row] : candidates)
            if (col == hoveredCell.first && row == hoveredCell.second)
            {
                isAreaHovered = true; break;
            }
    }

    for (auto& [col, row] : candidates)
    {
        if (col < 0 || col >= gridMap->GetCols()) continue;
        if (row < 0 || row >= gridMap->GetRows()) continue;

        auto& cell = gridMap->GetCell(col, row);

        if (data->type == CardType::Move &&
            (cell.type == CellType::Enemy || cell.type == CellType::Boss)) continue;

        m_playerHighlightCells.push_back({ col, row });

        bool isHovered = (col == hoveredCell.first && row == hoveredCell.second);
        bool isEnemy = (cell.type == CellType::Enemy || cell.type == CellType::Boss);

        // 攻撃カードは敵マスのみホバー有効
        if (data->type == CardType::Attack && isHovered && !isEnemy)
            isHovered = false;

        // 危険マス判定
        bool isDangerCell = false;
        for (auto enemy : enemies)
        {
            const EnemyAction* action = enemy->GetNextAction();
            if (!action || action->type != EnemyActionType::Attack) continue;

            if (IsInEnemyRange(col, row, action, enemy->gridCol, enemy->gridRow))
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
                    cell.gameObject.color = XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);
            }
            else
            {
                float brightness = isAreaHovered ? hoverBrightness : 0.6f;
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
                    cell.gameObject.color = XMFLOAT4(hoverBrightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, hoverBrightness, 0.2f, 1.0f);
                else if (data->type == CardType::Skill)
                    cell.gameObject.color = XMFLOAT4(0.2f, 0.2f, hoverBrightness, 1.0f);
                else if (data->type == CardType::Power)
                    cell.gameObject.color = XMFLOAT4(hoverBrightness, 0.2f, hoverBrightness, 1.0f);
            }
            else if (data->type == CardType::Move && isDangerCell)
            {
                float blink = 0.6f + 0.4f * sin(timer * 2.0f);
                cell.gameObject.color = XMFLOAT4(blink, blink, 0.1f, 1.0f);
            }
            else
            {
                if (data->type == CardType::Attack)
                    cell.gameObject.color = XMFLOAT4(brightness, 0.2f, 0.2f, 1.0f);
                else if (data->type == CardType::Move)
                    cell.gameObject.color = XMFLOAT4(0.2f, brightness, 0.4f, 1.0f);
                else if (data->type == CardType::Skill)
                    cell.gameObject.color = XMFLOAT4(0.2f, 0.2f, hoverBrightness, 1.0f);
                else if (data->type == CardType::Power)
                    cell.gameObject.color = XMFLOAT4(hoverBrightness, 0.2f, hoverBrightness, 1.0f);
            }
        }
    }
}

void BattleHighlighter::UpdateEnemyHighlight(
    const std::vector<Enemy*>& enemies,
    GridMap* gridMap,
    const Player* player,
    int playerCol, int playerRow,
    float timer)
{
    ClearEnemyHighlight(gridMap);

    for (auto enemy : enemies)
    {
        const EnemyAction* action = enemy->GetNextAction();
        if (!action || action->type != EnemyActionType::Attack) continue;

        auto candidates = GetCandidates(
            enemy->gridCol, enemy->gridRow,
            action->rangeType, action->range);

        for (auto& [col, row] : candidates)
        {
            if (col < 0 || col >= gridMap->GetCols()) continue;
            if (row < 0 || row >= gridMap->GetRows()) continue;

            auto& cell = gridMap->GetCell(col, row);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue;

            int dc = abs(col - enemy->gridCol);
            int dr = abs(row - enemy->gridRow);
            int dist = (action->rangeType == RangeType::Area)
                ? max(dc, dr) : (dc + dr);

            float alpha = 0.6f - (float)(dist - 1) / (float)max(1, action->range) * 0.25f;
            alpha = max(0.2f, min(0.6f, alpha));

            bool isPlayerHere = (col == playerCol && row == playerRow);

            if (isPlayerHere)
            {
                int finalDamage = action->value - player->GetBlock();
                if (finalDamage > 0)
                {
                    float pulse = sin(timer * 3.0f);
                    float blink = 0.3f + 0.7f * ((pulse + 1.0f) / 2.0f);
                    cell.gameObject.color = XMFLOAT4(blink, blink, 0.0f, 1.0f);
                }
                else
                    cell.gameObject.color = XMFLOAT4(alpha, alpha, 0.0f, 1.0f);
            }
            else
                cell.gameObject.color = XMFLOAT4(alpha, alpha, 0.0f, 1.0f);

            m_enemyHighlightCells.push_back({ col, row });
        }
    }
}