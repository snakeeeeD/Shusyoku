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

    // エネルギーが足りるかを先に確認
    if (player->GetEnergy() < data.cost)
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
                return result;
            }

            player->UseEnergy(data.cost);

            for (auto enemy : targets)
            {
                enemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
                // Thorns反射
                if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
                    player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());

                if (data.onHitEffect.hasEffect)
                {
                    if (data.onHitEffect.type == CardEffectType::Knockback)
                        ApplyKnockback(enemy, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies);
                    else if (data.onHitEffect.type == CardEffectType::Pull)
                        ApplyPull(enemy, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies, outNewPlayerCol, outNewPlayerRow);
                }
            }
        }
        else
        {
            Enemy* target = GetEnemyAt(targetCol, targetRow, enemies);
            int range = data.range;
            if (player->GetBuffManager().HasBuff(BuffType::Reposition))
                range += player->GetBuffManager().GetBuffValue(BuffType::Reposition);
            if (!target || GetMinDistToEnemy(playerCol, playerRow, target) > range)
            {
                return result;
            }
            player->UseEnergy(data.cost);
            target->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
            // Thorns反射
            if (target->GetBuffManager().HasBuff(BuffType::Thorns))
                player->TakeDamage(target->GetBuffManager().GetBuffValue(BuffType::Thorns));
            CardEffect::ApplyOnHitEffect(data.onHitEffect, target->GetBuffManager());

            // ノックバック/引き寄せ
            if (data.onHitEffect.hasEffect)
            {
                if (data.onHitEffect.type == CardEffectType::Knockback)
                    ApplyKnockback(target, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies);
                else if (data.onHitEffect.type == CardEffectType::Pull)
                    ApplyPull(target, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies, outNewPlayerCol, outNewPlayerRow);
            }
        }
        break;
    }
    case CardType::Move:
    {
        auto& cell = gridMap->GetCell(targetCol, targetRow);
        if (cell.type != CellType::Empty)
        {
            return result;
        }
        int dc = abs(playerCol - targetCol);
        int dr = abs(playerRow - targetRow);
        int moveRange = player->GetBuffManager().GetFinalMoveRange(data.range);
        if ((dc + dr) > moveRange)
        {
            return result;
        }
        player->UseEnergy(data.cost);
        gridMap->SetCellType(playerCol, playerRow, CellType::Empty);
        outNewPlayerCol = targetCol;
        outNewPlayerRow = targetRow;
        gridMap->SetCellType(targetCol, targetRow, CellType::Player);
        // 移動距離
        int moveDist = dc + dr;

        // Burn: 移動するたびダメージ
        if (player->GetBuffManager().HasBuff(BuffType::Burn))
            player->TakeDamage(player->GetBuffManager().GetBuffValue(BuffType::Burn) * moveDist);

        // Momentum: 移動するたびブロック+
        if (player->GetBuffManager().HasBuff(BuffType::Momentum))
            player->AddBlock(player->GetBuffManager().GetBuffValue(BuffType::Momentum) * moveDist);

        // Charge: 移動距離に応じて次の攻撃ダメージ+（AttackUpに加算）
        if (player->GetBuffManager().HasBuff(BuffType::Charge))
        {
            int bonus = player->GetBuffManager().GetBuffValue(BuffType::Charge) * moveDist;
            Buff atkBuff;
            atkBuff.type = BuffType::AttackUp;
            atkBuff.value = player->GetBuffManager().GetBuffValue(BuffType::AttackUp) + bonus;
            atkBuff.duration = 1;
            atkBuff.name = L"チャージ攻撃UP";
            atkBuff.description = L"";
            player->GetBuffManager().AddBuff(atkBuff);
        }

        // HitAndRun: 移動時に隣接する敵にダメージ
        if (player->GetBuffManager().HasBuff(BuffType::HitAndRun))
        {
            int hitDmg = player->GetBuffManager().GetBuffValue(BuffType::HitAndRun);
            for (auto enemy : enemies)
            {
                int ex = abs(targetCol - enemy->gridCol);
                int ey = abs(targetRow - enemy->gridRow);
                if (ex + ey == 1)
                    enemy->TakeDamage(hitDmg);
            }
        }
        break;
    }
    case CardType::Skill:
    {
        // 罠設置不可チェック（エナジー消費前）
        if (data.mainEffect.type == CardEffectType::PlaceTrap)
        {
            auto& cell = gridMap->GetCell(playerCol, playerRow);
            if (cell.trap.active)
                return result;
        }

        player->UseEnergy(data.cost);

        switch (data.mainEffect.type)
        {
        case CardEffectType::Block:
            player->AddBlock(player->GetBuffManager().GetFinalBlock(data.mainEffect.value));
            break;
        case CardEffectType::Draw:
            for (int i = 0; i < data.mainEffect.value; i++)
            {
                std::string id = deck.DrawCard();
                if (!id.empty())
                {
                    hand.AddCard(id);
                    result.drawnCards.push_back(id);
                }
            }
            break;
        case CardEffectType::Heal:
            player->Heal(data.mainEffect.value);
            break;
        case CardEffectType::AddEnergy:
            player->AddEnergy(data.mainEffect.value);
            break;
        case CardEffectType::ApplyBuff:
            CardEffect::ApplyEffectToPlayer(data.mainEffect, player);
            break;
        case CardEffectType::CreateCard:
            for (int i = 0; i < data.mainEffect.value; i++)
            {
                hand.AddCard(data.mainEffect.cardId);
                result.drawnCards.push_back(data.mainEffect.cardId);
            }
            break;
        case CardEffectType::PlaceTrap:
        {
            auto& cell = gridMap->GetCell(playerCol, playerRow);
            if (cell.trap.active)
            {
                return result;  // 既に罠がある → 使えない
            }
            cell.trap.active = true;
            cell.trap.type = StringToTrapType(data.mainEffect.trapType);
            cell.trap.value = data.mainEffect.value;
            cell.trap.duration = data.mainEffect.duration;
            break;
        }
        default:
            break;
        }
        break;
    }
    case CardType::Power:
    {
        player->UseEnergy(data.cost);
        CardEffect::ApplyEffectToPlayer(data.mainEffect, player);
        // パワーカードは捨て札に入れない
        hand.RemoveCard(cardIndex);
        return { true, true };
    }
    }

    // サブ効果の処理
    if (data.subEffect.hasEffect)
    {
        switch (data.subEffect.type)
        {
        case CardEffectType::Draw:
            for (int i = 0; i < data.subEffect.value; i++)
            {
                std::string id = deck.DrawCard();
                if (!id.empty())
                {
                    hand.AddCard(id);
                    result.drawnCards.push_back(id);
                }
            }
            break;
        case CardEffectType::Heal:
            player->Heal(data.subEffect.value);
            break;
        case CardEffectType::AddEnergy:
            player->AddEnergy(data.subEffect.value);
            break;
        case CardEffectType::ApplyBuff:
            CardEffect::ApplyEffectToPlayer(data.subEffect, player);
            break;
        case CardEffectType::Block:
            player->AddBlock(player->GetBuffManager().GetFinalBlock(data.subEffect.value));
            break;
        case CardEffectType::CreateCard:
            for (int i = 0; i < data.subEffect.value; i++)
            {
                hand.AddCard(data.subEffect.cardId);
                result.drawnCards.push_back(data.subEffect.cardId);
            }
            break;
        default:
            break;
        }
    }

    if (data.exhaust)
        deck.ExhaustCard(cardId);
    else
        deck.DiscardCard(cardId);

    hand.RemoveCard(cardIndex);
    result.success = true;
    result.cardUsed = true;
    return result;
}

void CardExecutor::TriggerTrap(Cell& cell, Enemy* enemy)
{
    if (!cell.trap.active) return;

    switch (cell.trap.type)
    {
    case TrapType::Explosion:
        enemy->TakeDamage(cell.trap.value);
        break;
    case TrapType::Root:
    {
        Buff root;
        root.type = BuffType::Root;
        root.value = cell.trap.value;
        root.duration = cell.trap.duration;
        root.name = L"拘束";
        root.description = L"";
        enemy->GetBuffManager().AddBuff(root);
        break;
    }
    case TrapType::Poison:
    {
        Buff poison;
        poison.type = BuffType::Poison;
        poison.value = cell.trap.value;
        poison.duration = cell.trap.duration;
        poison.name = L"毒";
        poison.description = L"";
        enemy->GetBuffManager().AddBuff(poison);
        break;
    }
    }
    cell.trap = TrapData();  // 発動後リセット
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
    for (int i = 0; i < distance; i++)
    {
        int nextCol = target->gridCol + dirC;
        int nextRow = target->gridRow + dirR;

        // 壁判定
        if (nextCol < 0 || nextCol >= gridMap->GetCols()
            || nextRow < 0 || nextRow >= gridMap->GetRows()
            || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
        {
            int remaining = distance - moved;
            target->TakeDamage(remaining * 3);
            break;
        }

        // 敵との衝突判定
        Enemy* blocker = GetEnemyAt(nextCol, nextRow, enemies);
        if (blocker && blocker != target)
        {
            int remaining = distance - moved;
            target->TakeDamage(remaining * 3);
            blocker->TakeDamage(remaining * 3);
            break;
        }

        // 移動実行
        gridMap->SetCellType(target->gridCol, target->gridRow, CellType::Empty);
        target->gridCol = nextCol;
        target->gridRow = nextRow;
        gridMap->SetCellType(nextCol, nextRow, CellType::Enemy);
        target->worldX = (nextCol - gridMap->GetCols() / 2.0f) * 1.1f;
        target->worldZ = (nextRow - gridMap->GetRows() / 2.0f) * 1.1f;
        auto& passedCell = gridMap->GetCell(nextCol, nextRow);
        TriggerTrap(passedCell, target);
        moved++;
    }
}

void CardExecutor::ApplyPull(Enemy* target, int playerCol, int playerRow,
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies,
    int& outNewPlayerCol, int& outNewPlayerRow)
{
    if (target->IsImmovable())
    {
        // プレイヤーが敵の方へ引っ張られる
        int dc = target->gridCol - playerCol;
        int dr = target->gridRow - playerRow;
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
            // 敵マスには入らない
            if (gridMap->GetCell(nextCol, nextRow).type == CellType::Enemy)
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
    for (int i = 0; i < distance; i++)
    {
        int nextCol = target->gridCol + dirC;
        int nextRow = target->gridRow + dirR;

        if (nextCol < 0 || nextCol >= gridMap->GetCols()
            || nextRow < 0 || nextRow >= gridMap->GetRows()
            || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
            break;

        // プレイヤーマスには入れない
        if (nextCol == playerCol && nextRow == playerRow)
            break;

        Enemy* blocker = GetEnemyAt(nextCol, nextRow, enemies);
        if (blocker && blocker != target)
            break;

        gridMap->SetCellType(target->gridCol, target->gridRow, CellType::Empty);
        target->gridCol = nextCol;
        target->gridRow = nextRow;
        gridMap->SetCellType(nextCol, nextRow, CellType::Enemy);
        target->worldX = (nextCol - gridMap->GetCols() / 2.0f) * 1.1f;
        target->worldZ = (nextRow - gridMap->GetRows() / 2.0f) * 1.1f;
        auto& passedCell = gridMap->GetCell(nextCol, nextRow);
        TriggerTrap(passedCell, target);
        moved++;
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

        if (nextCol < 0 || nextCol >= gridMap->GetCols()
            || nextRow < 0 || nextRow >= gridMap->GetRows()
            || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
        {
            preview.hitsWall = true;
            break;
        }

        Enemy* blocker = GetEnemyAt(nextCol, nextRow, enemies);
        if (blocker && blocker != target)
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
    int distance, GridMap* gridMap, std::vector<Enemy*>& enemies)
{
    MovePreview preview;
    preview.destCol = target->gridCol;
    preview.destRow = target->gridRow;

    if (target->IsImmovable())
    {
        preview.immovable = true;
        preview.playerDestCol = playerCol;
        preview.playerDestRow = playerRow;

        int dc = target->gridCol - playerCol;
        int dr = target->gridRow - playerRow;
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
            if (gridMap->GetCell(nextCol, nextRow).type == CellType::Enemy)
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

        if (nextCol < 0 || nextCol >= gridMap->GetCols()
            || nextRow < 0 || nextRow >= gridMap->GetRows()
            || gridMap->GetCell(nextCol, nextRow).type == CellType::Wall)
            break;

        if (nextCol == playerCol && nextRow == playerRow)
            break;

        Enemy* blocker = GetEnemyAt(nextCol, nextRow, enemies);
        if (blocker && blocker != target)
            break;

        preview.destCol = nextCol;
        preview.destRow = nextRow;
    }
    return preview;
}