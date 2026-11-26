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

// ノックバック・引き寄せ（実際の動きと予告）

// target を (nc, nr) へ動かせるか（形全体で見る）
static bool CanPush(Enemy* target, int nc, int nr, int playerCol, int playerRow,
    GridMap* gridMap, std::vector<Enemy*>& enemies, Enemy*& blocker)
{
    blocker = nullptr;
    for (auto& [dc, dr] : target->GetGridShape())
    {
        int c = nc + dc, r = nr + dr;
        if (c < 0 || c >= gridMap->GetCols() || r < 0 || r >= gridMap->GetRows()) return false;
        if (gridMap->GetCell(c, r).type == CellType::Wall) return false;
        if (c == playerCol && r == playerRow) return false;
        for (auto e : enemies)
            if (e != target && e->OccupiesCell(c, r)) { blocker = e; return false; }
    }
    return true;
}

void CardExecutor::ApplyKnockback(Enemy* target, int playerCol, int playerRow,
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies)
{
    if (target->IsImmovable())
        return;

    // プレイヤー→敵の方向を計算
    int dc = target->gridCol - playerCol;
    int dr = target->gridRow - playerRow;

    // 方向を正規化（縦横どちらか大きい方を優先）
    int dirC = 0, dirR = 0;
    if (abs(dc) >= abs(dr))
        dirC = (dc > 0) ? 1 : -1;
    else
        dirR = (dr > 0) ? 1 : -1;

    int moved = 0;
    std::vector<std::pair<float, float>> path;   // 通過するワールド座標（アニメ用）
    for (int i = 0; i < distance; i++)
    {
        int nextCol = target->gridCol + dirC;
        int nextRow = target->gridRow + dirR;

        // 壁・敵との衝突判定（形全体）
        Enemy* blocker = nullptr;
        bool canMove = CanPush(target, nextCol, nextRow, playerCol, playerRow, gridMap, enemies, blocker);
        if (!canMove && !blocker)
        {
            int remaining = distance - moved;
            target->TakeDamage(remaining * 3);
            break;
        }
        if (blocker)
        {
            int remaining = distance - moved;
            target->TakeDamage(remaining * 3);
            blocker->TakeDamage(remaining * 3);
            // 物理ドミノ：勢いを1減らして相手も吹き飛ばす（連鎖）
            if (remaining - 1 > 0)
                ApplyKnockback(blocker, playerCol, playerRow, remaining - 1, gridMap, enemies);
            break;
        }

        // 移動実行
        target->ClearFootprint(gridMap);
        target->gridCol = nextCol;
        target->gridRow = nextRow;
        target->MarkFootprint(gridMap);
        float wx, wz; target->CenterWorld(nextCol, nextRow, gridMap, wx, wz);
        path.push_back({ wx, wz });
        for (auto& [dc, dr] : target->GetGridShape())       // 形全体で罠を踏む
            TriggerTrap(gridMap->GetCell(nextCol + dc, nextRow + dr), target,
                nextCol + dc, nextRow + dr, gridMap, enemies);
        moved++;
    }
    if (!path.empty())   // 通ったマスを一気に滑って、最後だけ行き過ぎて戻る（途中のマスで跳ねない）
    {
        float os = 0.35f;                 // 行き過ぎ量（マス比）
        auto fin = path.back();
        path.push_back({ fin.first + dirC * os * 1.1f, fin.second + dirR * os * 1.1f }); // 行き過ぎ
        path.push_back(fin);                                                             // 戻って着地
        target->StartWalk(path, 0.05f);   // 速く（吹っ飛び）
        target->m_feel.SetDash(true, true);   // 飛ばされる間は土煙とブラー（止まったら自動で終わる）
        ScreenShake::Add(0.4f);           // 手応え
    }
}

void CardExecutor::ApplyPull(Enemy* target, int playerCol, int playerRow,
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
    int& outNewPlayerCol, int& outNewPlayerRow)
{
    if (target->IsImmovable())
    {
        // プレイヤーが、当たったマスの方へ引っ張られる（蛇の体など）
        int dc = target->HitCell().first - playerCol;
        int dr = target->HitCell().second - playerRow;
        int dirC = 0, dirR = 0;
        if (abs(dc) >= abs(dr))
            dirC = (dc > 0) ? 1 : -1;
        else
            dirR = (dr > 0) ? 1 : -1;

        for (int i = 0; i < distance; i++)
        {
            int nextCol = outNewPlayerCol + dirC;
            int nextRow = outNewPlayerRow + dirR;
            // 壁チェック
            if (nextCol < 0 || nextCol >= gridMap->GetCols()
                || nextRow < 0 || nextRow >= gridMap->GetRows()
                || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
                break;
            // 空きマスにしか入らない（敵もボスも止まる）
            if (gridMap->GetCell(nextCol, nextRow).type != CellType::Empty)
                break;

            gridMap->SetCellType(outNewPlayerCol, outNewPlayerRow, CellType::Empty);
            outNewPlayerCol = nextCol;
            outNewPlayerRow = nextRow;
            gridMap->SetCellType(nextCol, nextRow, CellType::Player);
        }
        return;
    }
    // 敵→プレイヤーの方向
    int dc = playerCol - target->gridCol;
    int dr = playerRow - target->gridRow;

    int dirC = 0, dirR = 0;
    if (abs(dc) >= abs(dr))
        dirC = (dc > 0) ? 1 : -1;
    else
        dirR = (dr > 0) ? 1 : -1;

    int moved = 0;
    std::vector<std::pair<float, float>> path;   // 通過するワールド座標（アニメ用）
    for (int i = 0; i < distance; i++)
    {
        int nextCol = target->gridCol + dirC;
        int nextRow = target->gridRow + dirR;

        Enemy* blocker = nullptr;                           // 形全体で判定（プレイヤーマスにも入れない）
        if (!CanPush(target, nextCol, nextRow, playerCol, playerRow, gridMap, enemies, blocker))
            break;

        target->ClearFootprint(gridMap);
        target->gridCol = nextCol;
        target->gridRow = nextRow;
        target->MarkFootprint(gridMap);
        float wx, wz; target->CenterWorld(nextCol, nextRow, gridMap, wx, wz);
        path.push_back({ wx, wz });

        for (auto& [dc, dr] : target->GetGridShape())       // 形全体で罠を踏む
            TriggerTrap(gridMap->GetCell(nextCol + dc, nextRow + dr), target,
                nextCol + dc, nextRow + dr, gridMap, enemies);
        moved++;
    }
    if (!path.empty())
    {
        float back = 0.35f, os = 0.3f;
        auto fin = path.back();
        std::vector<std::pair<float, float>> yank;
        yank.push_back({ target->worldX - dirC * back * 1.1f,
                         target->worldZ - dirR * back * 1.1f });
        yank.push_back({ fin.first + dirC * os * 1.1f, fin.second + dirR * os * 1.1f });
        yank.push_back(fin);
        target->StartWalk(yank, 0.06f);
        target->m_feel.SetDash(true, true);   // 引き寄せられる間も土煙とブラー
        ScreenShake::Add(0.4f);
    }
}

CardExecutor::MovePreview CardExecutor::PreviewKnockback(
    Enemy* target, int playerCol, int playerRow,
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies)
{
    MovePreview preview;
    preview.destCol = target->gridCol;
    preview.destRow = target->gridRow;

    if (target->IsImmovable())
    {
        preview.immovable = true;
        return preview;
    }

    int dc = target->gridCol - playerCol;
    int dr = target->gridRow - playerRow;
    int dirC = 0, dirR = 0;
    if (abs(dc) >= abs(dr))
        dirC = (dc > 0) ? 1 : -1;
    else
        dirR = (dr > 0) ? 1 : -1;

    for (int i = 0; i < distance; i++)
    {
        int nextCol = preview.destCol + dirC;
        int nextRow = preview.destRow + dirR;

        Enemy* blocker = nullptr;
        bool canMove = CanPush(target, nextCol, nextRow, playerCol, playerRow, gridMap, enemies, blocker);
        if (!canMove && !blocker)
        {
            preview.hitsWall = true;
            break;
        }
        if (blocker)
        {
            preview.hitsWall = true;
            preview.hasCollision = true;
            preview.collisionCol = blocker->gridCol;
            preview.collisionRow = blocker->gridRow;
            break;
        }

        preview.destCol = nextCol;
        preview.destRow = nextRow;
    }
    return preview;
}

CardExecutor::MovePreview CardExecutor::PreviewPull(
    Enemy* target, int playerCol, int playerRow,
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
    int aimCol, int aimRow)
{
    MovePreview preview;
    preview.destCol = target->gridCol;
    preview.destRow = target->gridRow;

    if (target->IsImmovable())
    {
        preview.immovable = true;
        preview.playerDestCol = playerCol;
        preview.playerDestRow = playerRow;

        int dc = aimCol - playerCol;
        int dr = aimRow - playerRow;
        int dirC = 0, dirR = 0;
        if (abs(dc) >= abs(dr))
            dirC = (dc > 0) ? 1 : -1;
        else
            dirR = (dr > 0) ? 1 : -1;

        for (int i = 0; i < distance; i++)
        {
            int nextCol = preview.playerDestCol + dirC;
            int nextRow = preview.playerDestRow + dirR;

            if (nextCol < 0 || nextCol >= gridMap->GetCols()
                || nextRow < 0 || nextRow >= gridMap->GetRows()
                || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
                break;
            if (gridMap->GetCell(nextCol, nextRow).type != CellType::Empty)
                break;

            preview.playerDestCol = nextCol;
            preview.playerDestRow = nextRow;
        }
        return preview;
    }

    int dc = playerCol - target->gridCol;
    int dr = playerRow - target->gridRow;
    int dirC = 0, dirR = 0;
    if (abs(dc) >= abs(dr))
        dirC = (dc > 0) ? 1 : -1;
    else
        dirR = (dr > 0) ? 1 : -1;

    for (int i = 0; i < distance; i++)
    {
        int nextCol = preview.destCol + dirC;
        int nextRow = preview.destRow + dirR;

        Enemy* blocker = nullptr;
        if (!CanPush(target, nextCol, nextRow, playerCol, playerRow, gridMap, enemies, blocker))
            break;

        preview.destCol = nextCol;
        preview.destRow = nextRow;
    }
    return preview;
}
