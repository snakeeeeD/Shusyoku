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
        Enemy* multiHitTarget = nullptr;   // 追撃対象
        int    multiHitRemain = 0;         // 残りヒット数
        int    multiHitDamage = 0;         // 1発の確定ダメージ
        bool startChainDetonate = false;
        int  chainCol = 0, chainRow = 0;
        bool chainFull = false;
    };

    struct MovePreview 
    {
        bool immovable = false;
        int destCol, destRow;      // 移動先
        bool hitsWall = false;     // 壁or敵にぶつかるか

        // Pull: immovable時のプレイヤー移動先
        int playerDestCol = -1, playerDestRow = -1;
        // Knockback: 後ろの敵に衝突
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

    static bool DetonateTrap(Cell& cell, int col, int row,
        GridMap* gridMap, std::vector<Enemy*>& enemies,
        bool fullPower = false, bool chain = false);

private:
    std::vector<Enemy*> GetEnemiesInRange(
        const CardData& data,
        int playerCol, int playerRow,
        std::vector<Enemy*>& enemies, int aimDx, int aimDy);

    static Enemy* GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies);

    void ApplyKnockback(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies);
    void ApplyPull(Enemy* target, int playerCol, int playerRow,
        int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
        int& outNewPlayerCol, int& outNewPlayerRow);
};