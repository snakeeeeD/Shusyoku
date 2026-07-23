#include "CardExecutor.h"
#include "CardEffect.h"
#include "EffectManager.h"
#include "BattleHighlighter.h"
#include "TerrainDataBase.h"
#include "RangeShape.h"
#include <algorithm>
#include <queue>
#include <map>

Enemy* CardExecutor::GetEnemyAt(int col, int row, std::vector<Enemy*>& enemies)
{
    for (auto enemy : enemies)
        for (auto& [dc, dr] : enemy->GetGridShape())
            if (enemy->gridCol + dc == col && enemy->gridRow + dr == row)
                return enemy;
    return nullptr;
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
    int& outNewPlayerCol, int& outNewPlayerRow,
    const std::vector<std::pair<int, int>>* explicitPath)
{
    outNewPlayerCol = playerCol;
    outNewPlayerRow = playerRow;

    ExecuteResult result = { false, false };

    // エネルギーが足りるかを先に確認
    if (player->GetEnergy() < data.cost)
        return result;

    int pendingDiscard = 0;

    struct SlotGuard {
        Hand& h;
        SlotGuard(Hand& hand) : h(hand) { h.ReserveSlot(true); }
        ~SlotGuard() { h.ReserveSlot(false); }
    } slotGuard(hand);

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
            if (data.pierce)
            {
                int dx = 0, dy = 0;
                if (targetCol > playerCol) dx = 1;
                else if (targetCol < playerCol) dx = -1;
                if (targetRow > playerRow) dy = 1;
                else if (targetRow < playerRow) dy = -1;

                if ((dx != 0 && dy != 0) || (dx == 0 && dy == 0))
                    return result;

                // 先にライン上に敵がいるか確認
                bool hasTarget = false;
                int col = playerCol;
                int row = playerRow;
                for (int step = 0; step < data.range; step++)
                {
                    col += dx;
                    row += dy;
                    if (col < 0 || col >= gridMap->GetCols()
                        || row < 0 || row >= gridMap->GetRows())
                        break;
                    auto& cell = gridMap->GetCell(col, row);
                    if (cell.type == CellType::Wall)
                        break;
                    if (GetEnemyAt(col, row, enemies))
                        hasTarget = true;
                }

                if (!hasTarget)
                    return result;

                player->UseEnergy(data.cost);

                col = playerCol;
                row = playerRow;
                for (int step = 0; step < data.range; step++)
                {
                    col += dx;
                    row += dy;
                    if (col < 0 || col >= gridMap->GetCols()
                        || row < 0 || row >= gridMap->GetRows())
                        break;
                    auto& cell = gridMap->GetCell(col, row);
                    if (cell.type == CellType::Wall)
                        break;
                    Enemy* enemy = GetEnemyAt(col, row, enemies);
                    if (enemy)
                    {
                        enemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
                        if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
                            player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                        CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
                    }
                }

                result.cardUsed = true;
                break;
            }

            Enemy* target = GetEnemyAt(targetCol, targetRow, enemies);
            int range = data.range;
            if (player->GetBuffManager().HasBuff(BuffType::Reposition))
                range += player->GetBuffManager().GetBuffValue(BuffType::Reposition);
            if (!target)
            {
                // dashで空マスクリック → 移動のみ
                if (data.dash)
                {
                    // 方向を決定
                    int dx = 0, dy = 0;
                    if (targetCol > playerCol) dx = 1;
                    else if (targetCol < playerCol) dx = -1;
                    if (targetRow > playerRow) dy = 1;
                    else if (targetRow < playerRow) dy = -1;

                    // 縦か横のみ（斜め不可）
                    if ((dx != 0 && dy != 0) || (dx == 0 && dy == 0))
                        return result;

                    player->UseEnergy(data.cost);

                    int moveCol = playerCol;
                    int moveRow = playerRow;
                    Enemy* hitEnemy = nullptr;

                    for (int step = 0; step < data.range; step++)
                    {
                        int nextCol = moveCol + dx;
                        int nextRow = moveRow + dy;

                        if (nextCol < 0 || nextCol >= gridMap->GetCols()
                            || nextRow < 0 || nextRow >= gridMap->GetRows())
                            break;

                        Enemy* enemy = GetEnemyAt(nextCol, nextRow, enemies);
                        if (enemy)
                        {
                            hitEnemy = enemy;
                            break;
                        }

                        if (gridMap->GetCell(nextCol, nextRow).type != CellType::Empty)
                            break;

                        auto& passCell = gridMap->GetCell(nextCol, nextRow);
                        if (passCell.tileEffect.active)
                            TriggerTerrain(passCell, player);

                        moveCol = nextCol;
                        moveRow = nextRow;
                    }

                    // 移動
                    if (moveCol != playerCol || moveRow != playerRow)
                    {
                        gridMap->SetCellType(playerCol, playerRow, CellType::Empty);
                        outNewPlayerCol = moveCol;
                        outNewPlayerRow = moveRow;
                        gridMap->SetCellType(moveCol, moveRow, CellType::Player);

                        int moveDist = abs(moveCol - playerCol) + abs(moveRow - playerRow);

                        if (player->GetBuffManager().HasBuff(BuffType::Burn))
                            player->TakeDamage(player->GetBuffManager().GetBuffValue(BuffType::Burn) * moveDist, DamageFeel::Burn);

                        if (player->GetBuffManager().HasBuff(BuffType::Momentum))
                            player->AddBlock(player->GetBuffManager().GetBuffValue(BuffType::Momentum) * moveDist);

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

                        if (player->GetBuffManager().HasBuff(BuffType::HitAndRun))
                        {
                            int hitDmg = player->GetBuffManager().GetBuffValue(BuffType::HitAndRun);
                            for (auto enemy : enemies)
                            {
                                int ex = abs(moveCol - enemy->gridCol);
                                int ey = abs(moveRow - enemy->gridRow);
                                if (ex + ey == 1)
                                    enemy->TakeDamage(hitDmg);
                            }
                        }
                    }

                    // ダメージ
                    if (hitEnemy)
                    {
                        hitEnemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
                        if (hitEnemy->GetBuffManager().HasBuff(BuffType::Thorns))
                            player->TakeDamage(hitEnemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                        CardEffect::ApplyOnHitEffect(data.onHitEffect, hitEnemy->GetBuffManager());
                    }

                    result.cardUsed = true;
                    break;
                }
                
                return result;
            }
            // 表示している形状と同じ判定（敵は複数マス占有しうるので、どれか1マスでも範囲内ならOK）
            bool inShape = false;
            for (auto& [dc, dr] : target->GetGridShape())
                if (RangeShape::Contains(playerCol, playerRow,
                    target->gridCol + dc, target->gridRow + dr, data.rangeType, range))
                {
                    inShape = true; break;
                }
            if (!inShape)
                return result;
            player->UseEnergy(data.cost);

            // dash: 敵の手前まで移動
            if (data.dash && target)
            {
                int dx = 0, dy = 0;
                if (playerCol < target->gridCol) dx = 1;
                else if (playerCol > target->gridCol) dx = -1;
                if (playerRow < target->gridRow) dy = 1;
                else if (playerRow > target->gridRow) dy = -1;

                int destCol = target->gridCol - dx;
                int destRow = target->gridRow - dy;

                if ((destCol != playerCol || destRow != playerRow)
                    && destCol >= 0 && destCol < gridMap->GetCols()
                    && destRow >= 0 && destRow < gridMap->GetRows()
                    && gridMap->GetCell(destCol, destRow).type == CellType::Empty)
                {
                    gridMap->SetCellType(playerCol, playerRow, CellType::Empty);
                    int cx = playerCol, cy = playerRow;
                    while (cx != destCol || cy != destRow)
                    {
                        cx += dx;
                        cy += dy;
                        if (cx == destCol && cy == destRow) break;
                        auto& passCell = gridMap->GetCell(cx, cy);
                        if (passCell.tileEffect.active)
                            TriggerTerrain(passCell, player);
                    }
                    outNewPlayerCol = destCol;
                    outNewPlayerRow = destRow;
                    gridMap->SetCellType(destCol, destRow, CellType::Player);
                }
            }

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
                 int moveRange = player->GetBuffManager().GetFinalMoveRange(data.range);

                 std::vector<std::pair<int, int>> path;

                 if (explicitPath && !explicitPath->empty())
                 {
                     // 手動経路：検証して採用
                     if ((int)explicitPath->size() > moveRange)
                         return result;

                     int pc = playerCol, pr = playerRow;
                     for (auto& [c, r] : *explicitPath)
                     {
                         if (c < 0 || c >= gridMap->GetCols() || r < 0 || r >= gridMap->GetRows())
                             return result;
                         if (abs(c - pc) + abs(r - pr) != 1)
                             return result;
                         if (gridMap->GetCell(c, r).type != CellType::Empty)
                             return result;
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
                         return result;

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

                     if (!found) return result;

                     auto cur = goalPos;
                     while (cur != startPos)
                     {
                         path.push_back(cur);
                         cur = parent[cur];
                     }
                     std::reverse(path.begin(), path.end());
                 }

                 if (path.empty())
                     return result;

           player->UseEnergy(data.cost);
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

           if (player->GetBuffManager().HasBuff(BuffType::Momentum))
               player->AddBlock(player->GetBuffManager().GetBuffValue(BuffType::Momentum) * moveDist);

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
            if (cell.tileEffect.active)
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
        case CardEffectType::UpgradeHand:
            hand.UpgradeAll();
            break;
        case CardEffectType::CreateCard:
            for (int i = 0; i < data.mainEffect.value; i++)
            {
                hand.AddCard(data.mainEffect.cardId);
                result.drawnCards.push_back(data.mainEffect.cardId);
            }
            break;
        case CardEffectType::Discard:
            pendingDiscard += data.mainEffect.value;
            break;
        case CardEffectType::PlaceTrap:
        {
            auto& cell = gridMap->GetCell(playerCol, playerRow);
            if (cell.tileEffect.active)
            {
                return result;
            }
            cell.tileEffect.active = true;
            cell.tileEffect.id = data.mainEffect.trapType;
            cell.tileEffect.value = data.mainEffect.value;
            cell.tileEffect.duration = data.mainEffect.duration;
            const TerrainDef* tDef = TerrainDataBase::Get(data.mainEffect.trapType);
            if (tDef) cell.tileEffect.persistent = tDef->persistent;
            break;
        }
        case CardEffectType::Search:
        case CardEffectType::Salvage:
            result.pendingSelection = data.mainEffect.type;
            break;
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
        case CardEffectType::Discard:
            pendingDiscard += data.subEffect.value;
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
    if (pendingDiscard > 0) hand.DiscardRandom(pendingDiscard);   // 使ったカードを除いてから捨てる
    result.pendingDiscard = pendingDiscard;      // 選択はシーン側に任せる
    result.success = true;
    if (data.selfDamage > 0)
        player->TakeDamage(data.selfDamage);
    result.cardUsed = true;

    if (!data.vfx.empty())
    {
        float vx = (targetCol - gridMap->GetCols() / 2.0f) * 1.1f;
        float vz = (targetRow - gridMap->GetRows() / 2.0f) * 1.1f;
        EffectManager::Play(data.vfx, vx, 0.5f, vz);
    }

    return result;
}

void CardExecutor::TriggerTrap(Cell& cell, Enemy* enemy, int col, int row,
    GridMap* gridMap, std::vector<Enemy*>& enemies)
{
    if (!cell.tileEffect.active) return;
    if (cell.tileEffect.persistent) return;

    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return;

    if (def->effect == "Damage")
    {
        enemy->TakeDamage(cell.tileEffect.value);

        if (def->aoe)
        {
            int halfDmg = cell.tileEffect.value / 2;
            for (int dr = -1; dr <= 1; dr++)
            {
                for (int dc = -1; dc <= 1; dc++)
                {
                    if (dc == 0 && dr == 0) continue;
                    int nc = col + dc;
                    int nr = row + dr;
                    if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows())
                        continue;
                    Enemy* nearby = GetEnemyAt(nc, nr, enemies);
                    if (nearby && nearby != enemy)
                        nearby->TakeDamage(halfDmg);
                }
            }
        }
    }
    else if (def->effect == "ApplyDebuff")
    {
        Buff debuff;
        debuff.type = StringToBuffType(def->buffType);
        debuff.value = cell.tileEffect.value;
        debuff.duration = def->buffDuration;
        debuff.name = def->name;
        debuff.description = L"";
        enemy->GetBuffManager().AddBuff(debuff);
    }

    if (!cell.tileEffect.persistent)
        cell.tileEffect = TileEffect();
}

void CardExecutor::TriggerTerrain(Cell& cell, Player* player)
{
    if (!cell.tileEffect.active) return;
    if (!cell.tileEffect.persistent) return;  // 罠はここでは発動しない

    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return;

    if (def->effect == "Damage")
    {
        player->TakeDamage(cell.tileEffect.value);
    }
    else if (def->effect == "ApplyDebuff")
    {
        Buff debuff;
        debuff.type = StringToBuffType(def->buffType);
        debuff.value = cell.tileEffect.value;
        debuff.duration = def->buffDuration;
        debuff.name = def->name;
        debuff.description = L"";
        player->GetBuffManager().AddBuff(debuff);
    }

    // duration管理（-1は永続）
    if (cell.tileEffect.duration > 0)
    {
        cell.tileEffect.duration--;
        if (cell.tileEffect.duration <= 0)
            cell.tileEffect = TileEffect();
    }
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
        TriggerTrap(passedCell, target, nextCol, nextRow, gridMap, enemies);
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
        TriggerTrap(passedCell, target, nextCol, nextRow, gridMap, enemies);
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