#pragma once
#include "CardData.h"
#include "Player.h"
#include "Enemy.h"
#include "GridMap.h"
#include "Hand.h"
#include "Deck.h"
#include <vector>

class CardExecutor
{
public:
    struct ExecuteResult
    {
        bool success;
        bool cardUsed;
    };

    ExecuteResult Execute(
        const CardData& data,
        const std::string& cardId,
        int targetCol, int targetRow,
        Player* player,
        std::vector<Enemy*>& enemies,
        GridMap* gridMap,
        int playerCol, int playerRow,
        Hand& hand, int cardIndex,
        Deck& deck,
        int& outNewPlayerCol,
        int& outNewPlayerRow
    );

private:
    std::vector<Enemy*> GetEnemiesInRange(
        const CardData& data,
        int playerCol, int playerRow,
        std::vector<Enemy*>& enemies);

    Enemy* GetEnemyAt(
        int col, int row,
        std::vector<Enemy*>& enemies);

    int GetMinDistToEnemy(
        int playerCol, int playerRow,
        Enemy* enemy);
};