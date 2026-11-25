#include "CardExecutor.h"
#include "CardEffect.h"
#include "CardDamage.h"
#include "EffectManager.h"
#include "BattleHighlighter.h"
#include "TerrainDataBase.h"
#include "RelicManager.h"
#include "CardDataBase.h"
#include "RangeShape.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Se.h"
#include "Faith.h"
#include <algorithm>
#include <queue>
#include <map>

// 移動カード（経路・地形・付随効果）
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// 移動カード：経路を決めて動き、通ったマスの地形・やけど・付随効果
bool CardExecutor::Play::Move()
{
    std::vector<std::pair<int, int>> path;
    if (!FindMovePath(path)) return false;

    player->UseEnergy(payCost);
    gridMap->SetCellType(playerCol, playerRow, CellType::Empty);
    outNewPlayerCol = targetCol;
    outNewPlayerRow = targetRow;
    gridMap->SetCellType(targetCol, targetRow, CellType::Player);

    // 通過マスの地形効果（最後のマスはBattleScene側で処理）
    for (int i = 0; i < (int)path.size() - 1; i++)
    {
        auto& passCell = gridMap->GetCell(path[i].first, path[i].second);
        if (passCell.tileEffect.active)
            TriggerTerrain(passCell, player);
    }

    // 移動距離（実際の経路長）
    int moveDist = (int)path.size();

    if (player->GetBuffManager().HasBuff(BuffType::Burn))
        player->TakeDamage(player->GetBuffManager().GetBuffValue(BuffType::Burn) * moveDist, DamageFeel::Burn);

    result.moveDist = moveDist;   // 移動の効果は BattleScene::OnPlayerMoved

    ApplyMoveExtras();
    return true;
}

// 移動の経路：手で引いた経路なら確かめて使い、無ければ最短経路を探す（行き先 targetCol/Row も決まる）
bool CardExecutor::Play::FindMovePath(std::vector<std::pair<int, int>>& path)
{
    int moveRange = player->GetMoveRange(data.range);


    if (explicitPath && !explicitPath->empty())
    {
        // 手動経路：検証して採用
        if ((int)explicitPath->size() > moveRange)
            return false;

        int pc = playerCol, pr = playerRow;
        for (auto& [c, r] : *explicitPath)
        {
            if (c < 0 || c >= gridMap->GetCols() || r < 0 || r >= gridMap->GetRows())
                return false;
            if (abs(c - pc) + abs(r - pr) != 1)
                return false;
            if (gridMap->GetCell(c, r).type != CellType::Empty)
                return false;
            pc = c; pr = r;
        }
        path = *explicitPath;
        targetCol = path.back().first;
        targetRow = path.back().second;
    }
    else
    {
        auto& cell = gridMap->GetCell(targetCol, targetRow);
        if (cell.type != CellType::Empty)
            return false;

        // BFS で経路探索
        std::queue<std::pair<int, int>> bfsQueue;
        std::map<std::pair<int, int>, int> dist;
        std::map<std::pair<int, int>, std::pair<int, int>> parent;

        auto startPos = std::make_pair(playerCol, playerRow);
        auto goalPos = std::make_pair(targetCol, targetRow);

        bfsQueue.push(startPos);
        dist[startPos] = 0;
        parent[startPos] = { -1, -1 };

        const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
        bool found = false;

        while (!bfsQueue.empty())
        {
            auto [col, row] = bfsQueue.front();
            bfsQueue.pop();

            if (col == targetCol && row == targetRow) { found = true; break; }
            if (dist[{col, row}] >= moveRange) continue;

            for (int d = 0; d < 4; d++)
            {
                int nc = col + dirs[d][0];
                int nr = row + dirs[d][1];
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
                auto np = std::make_pair(nc, nr);
                if (dist.count(np)) continue;
                if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
                dist[np] = dist[{col, row}] + 1;
                parent[np] = { col, row };
                bfsQueue.push(np);
            }
        }

        if (!found) return false;

        auto cur = goalPos;
        while (cur != startPos)
        {
            path.push_back(cur);
            cur = parent[cur];
        }
        std::reverse(path.begin(), path.end());
    }

    if (path.empty())
        return false;
    return true;
}

// 移動カード自身の付随効果（移動＋ブロック・ドロー）
void CardExecutor::Play::ApplyMoveExtras()
{
    switch (data.mainEffect.type)
    {
    case CardEffectType::Block:
        player->AddBlock(data.mainEffect.value);
        break;
    case CardEffectType::Draw:
        for (int i = 0; i < data.mainEffect.value; i++)
        {
            std::string id = deck.DrawCard();
            if (!id.empty()) { hand.AddCard(id); result.drawnCards.push_back(id); }
        }
        break;
    default: break;
    }
}
