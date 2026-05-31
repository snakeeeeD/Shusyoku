#include "CardExecutor.h"
#include "CardEffect.h"
#include "BattleHighlighter.h"
#include <algorithm>

Enemy* CardExecutor::GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies)
{
    for (auto enemy : enemies)
        for (auto& [dc, dr] : enemy->GetGridShape())
            if (enemy->gridCol + dc == col && enemy->gridRow + dr == row)
                return enemy;
    return nullptr;
}

int CardExecutor::GetMinDistToEnemy(int playerCol, int playerRow, Enemy* enemy)
{
    int minDist = INT_MAX;
    for (auto& [dc, dr] : enemy->GetGridShape())
    {
        int dist = abs(playerCol - (enemy->gridCol + dc))
            + abs(playerRow - (enemy->gridRow + dr));
        minDist = min(minDist, dist);
    }
    return minDist;
}

std::vector<Enemy*> CardExecutor::GetEnemiesInRange(
    const CardData& data, int playerCol, int playerRow,
    std::vector<Enemy*>& enemies)
{
    std::vector<Enemy*> result;
    auto candidates = BattleHighlighter::GetCandidates(
        playerCol, playerRow, data.rangeType, data.range);

    for (auto enemy : enemies)
    {
        bool inRange = false;
        for (auto& [dc, dr] : enemy->GetGridShape())
        {
            int col = enemy->gridCol + dc;
            int row = enemy->gridRow + dr;
            for (auto& [cc, cr] : candidates)
                if (cc == col && cr == row) { inRange = true; break; }
            if (inRange) break;
        }
        if (inRange) result.push_back(enemy);
    }
    return result;
}

CardExecutor::ExecuteResult CardExecutor::Execute(
    const CardData& data, const std::string& cardId,
    int targetCol, int targetRow,
    Player* player, std::vector<Enemy*>& enemies,
    GridMap* gridMap, int playerCol, int playerRow,
    Hand& hand, int cardIndex, Deck& deck,
    int& outNewPlayerCol, int& outNewPlayerRow)
{
    outNewPlayerCol = playerCol;
    outNewPlayerRow = playerRow;

    ExecuteResult result = { false, false };

    if (!player->UseEnergy(data.cost))
        return result;

    switch (data.type)
    {
    case CardType::Attack:
    {
        if (data.rangeType == RangeType::Area)
        {
            auto targets = GetEnemiesInRange(data, playerCol, playerRow, enemies);
            if (targets.empty())
            {
                //player->AddEnergy(data.cost); // エネルギーを返す
                return result;
            }
            for (auto enemy : targets)
            {
                enemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
                CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
            }
        }
        else
        {
            Enemy* target = GetEnemyAt(targetCol, targetRow, enemies);
            if (!target || GetMinDistToEnemy(playerCol, playerRow, target) > data.range)
            {
                //player->AddEnergy(data.cost);
                return result;
            }
            target->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
            CardEffect::ApplyOnHitEffect(data.onHitEffect, target->GetBuffManager());
        }
        break;
    }
    case CardType::Move:
    {
        auto& cell = gridMap->GetCell(targetCol, targetRow);
        if (cell.type != CellType::Empty)
        {
            //player->AddEnergy(data.cost);
            return result;
        }
        int dc = abs(playerCol - targetCol);
        int dr = abs(playerRow - targetRow);
        if ((dc + dr) > data.range)
        {
            //player->AddEnergy(data.cost);
            return result;
        }
        gridMap->SetCellType(playerCol, playerRow, CellType::Empty);
        outNewPlayerCol = targetCol;
        outNewPlayerRow = targetRow;
        gridMap->SetCellType(targetCol, targetRow, CellType::Player);
        break;
    }
    case CardType::Skill:
    {
        int finalBlock = player->GetBuffManager().GetFinalBlock(data.mainEffect.value);
        player->AddBlock(finalBlock);
        break;
    }
    case CardType::Power:
    {
        CardEffect::ApplyEffectToPlayer(data.mainEffect, player);
        // パワーカードは捨て札に入れない
        hand.RemoveCard(cardIndex);
        return { true, true };
    }
    }

    deck.DiscardCard(cardId);
    hand.RemoveCard(cardIndex);
    result.success = true;
    result.cardUsed = true;
    return result;
}