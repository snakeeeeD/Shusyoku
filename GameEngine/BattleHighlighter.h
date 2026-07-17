#pragma once
#include "CardData.h"
#include "Enemy.h"
#include "GridMap.h"
#include "Player.h"
#include <vector>
#include <utility>
#include <map>

class Renderer3D;

class BattleHighlighter
{
public:
    // 範囲内のマスを取得（静的・汎用）
    static std::vector<std::pair<int, int>> GetCandidates(
        int centerCol, int centerRow,
        RangeType rangeType, int range);

    void UpdatePlayerHighlight(
        int centerCol, int centerRow,
        const CardData* data,
        const std::vector<Enemy*>& enemies,
        GridMap* gridMap,
        const Player* player,
        float timer,
        std::pair<int, int> hoveredCell,
        Renderer3D* renderer3D,
        int screenWidth, int screenHeight,
        const RECT& cardArea);

    void UpdateEnemyHighlight(
        const std::vector<Enemy*>& enemies,
        GridMap* gridMap,
        const Player* player,
        int playerCol, int playerRow,
        float timer);

    void ClearPlayerHighlight(GridMap* gridMap);
    void ClearEnemyHighlight(GridMap* gridMap);

    const std::vector<std::pair<int, int>>& GetPlayerHighlightCells() const
    {
        return m_playerHighlightCells;
    }

    const std::vector<std::pair<int, int>>& GetOutOfRangeCells() const
    {
        return m_outOfRangeCells;
    }

    void SetTravelPath(const std::vector<std::pair<int, int>>* p) { m_travelPath = p; }

    void SetSelectedEnemy(int i) { m_selectedEnemy = i; }

private:
    std::vector<std::pair<int, int>> m_playerHighlightCells;
    std::vector<std::pair<int, int>> m_enemyHighlightCells;
    std::vector<std::pair<int, int>> m_outOfRangeCells;
    const std::vector<std::pair<int, int>>* m_travelPath = nullptr;

    float m_enemyCycleTimer = 0.0f;

    int m_selectedEnemy = -1;
};