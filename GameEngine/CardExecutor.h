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
        std::vector<std::string> drawnCards;
        CardEffectType pendingSelection = CardEffectType::None;
        int pendingDiscard = 0;
    };

    struct MovePreview 
    {
        bool immovable = false;
        int destCol, destRow;      // ˆÚ“®æ
        bool hitsWall = false;     // •Çor“G‚É‚Ô‚Â‚©‚é‚©

        // Pull: immovable‚ÌƒvƒŒƒCƒ„[ˆÚ“®æ
        int playerDestCol = -1, playerDestRow = -1;
        // Knockback: Œã‚ë‚Ì“G‚ÉÕ“Ë
        bool hasCollision = false;
        int collisionCol = -1, collisionRow = -1;
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
        int& outNewPlayerRow,
        const std::vector<std::pair<int, int>>* explicitPath = nullptr
    );

    static MovePreview PreviewKnockback(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);
    static MovePreview PreviewPull(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);

    static void TriggerTrap(Cell& cell, Enemy* enemy, int col, int row,
        GridMap* gridMap, std::vector<Enemy*>& enemies);

    static void TriggerTerrain(Cell& cell, Player* player);

private:
    std::vector<Enemy*> GetEnemiesInRange(
        const CardData& data,
        int playerCol, int playerRow,
        std::vector<Enemy*>& enemies);

    static Enemy* GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies);

    void ApplyKnockback(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);
    void ApplyPull(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
        int& outNewPlayerCol, int& outNewPlayerRow);
};