#include "CardExecutor.h"
#include "CardEffect.h"
#include "EffectManager.h"
#include "BattleHighlighter.h"
#include "TerrainDataBase.h"
#include "RelicManager.h"
#include "RangeShape.h"
#include "ScreenShake.h"
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
    std::vector<Enemy*>& enemies, int aimDx, int aimDy)
{
    std::vector<Enemy*> result;
    auto candidates = BattleHighlighter::GetCandidates(
        playerCol, playerRow, data.rangeType, data.range, aimDx, aimDy);

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

static int EffectiveHits(const CardData& data)
{
    int h = data.hits;
    if (data.type == CardType::Attack) h += RelicManager::SumValue("multiHit");
    return h < 1 ? 1 : h;
}

// onHitデバフ付与時のVFX（buffType → effects.jsonのID）
static void PlayOnHitVfx(const CardEffectData& e, Enemy* enemy, GridMap* gridMap)
{
    if (!e.hasEffect || e.buffType.empty() || !enemy) return;

    std::string fx;
    const std::string& b = e.buffType;
    if (b == "Poison")     fx = "poison_apply";
    else if (b == "Weak")       fx = "weaken";
    else if (b == "Vulnerable") fx = "expose";
    else if (b == "Root")       fx = "bind";
    else if (b == "Burn")       fx = "burn";
    else return;   // 対応エフェクトが無い種は出さない

    float wx = (enemy->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (enemy->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    EffectManager::Play(fx, wx, 0.5f, wz);

    if (b == "Poison")
    {
        int shown = e.value + RelicManager::SumValue("poisonAdd");   // 毒の心得ぶんも表示
        FloatingTextManager::Spawn(wx, 0.7f, wz, std::to_wstring(shown),
            BuffInfo::Get(BuffType::Poison).color, 32.0f);
    }
}

static void ApplyAllEnemyEffect(const CardData& data, std::vector<Enemy*>& enemies)
{
    if (!data.allEnemyEffect.hasEffect || data.allEnemyEffect.buffType.empty()) return;
    BuffType bt = StringToBuffType(data.allEnemyEffect.buffType);
    for (auto e : enemies)
    {
        Buff b; b.type = bt;
        b.value = data.allEnemyEffect.value;
        b.duration = data.allEnemyEffect.duration;
        b.name = BuffInfo::Get(bt).name; b.description = L"";
        e->GetBuffManager().AddBuff(b);
    }
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
        if (data.rangeType == RangeType::Area || data.rangeType == RangeType::Cone)
        {
            int aimDx = 0, aimDy = 0;
            if (data.rangeType == RangeType::Cone)
            {
                RangeShape::CardinalAim(playerCol, playerRow, targetCol, targetRow, aimDx, aimDy);
                if (aimDx == 0 && aimDy == 0) aimDy = -1;   // 真上/未指定は上向き
            }
            auto targets = GetEnemiesInRange(data, playerCol, playerRow, enemies, aimDx, aimDy);
            if (targets.empty())
            {
                return result;
            }

            player->UseEnergy(data.cost);

            int baseVal = data.mainEffect.value;
            if (data.scaleByTrapCount)
            {
                int traps = 0;
                for (int r = 0; r < gridMap->GetRows(); r++)
                    for (int c = 0; c < gridMap->GetCols(); c++)
                        if (gridMap->GetCell(c, r).tileEffect.active) traps++;
                baseVal *= traps;   // 盤面の罠数だけ倍
            }

            int hits = EffectiveHits(data);
            int dmg = player->GetBuffManager().GetFinalAttack(baseVal);
            for (auto enemy : targets)
            {
                enemy->TakeDamage(dmg);

                if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
                    player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
                PlayOnHitVfx(data.onHitEffect, enemy, gridMap);

                if (data.onHitEffect.hasEffect)
                {
                    if (data.onHitEffect.type == CardEffectType::Knockback)
                        ApplyKnockback(enemy, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies);
                    else if (data.onHitEffect.type == CardEffectType::Pull)
                        ApplyPull(enemy, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies, outNewPlayerCol, outNewPlayerRow);
                }
            }                            
            if (hits > 1)        
            {
                result.multiHitTargets = targets;
                result.multiHitRemain = hits - 1;
                result.multiHitDamage = dmg;
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
                        for (int h = 0; h < EffectiveHits(data); h++)
                            enemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));

                        if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
                            player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                        CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
                        PlayOnHitVfx(data.onHitEffect, enemy, gridMap);
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
                        for (int h = 0; h < EffectiveHits(data); h++)
                            hitEnemy->TakeDamage(player->GetBuffManager().GetFinalAttack(data.mainEffect.value));
                        if (hitEnemy->GetBuffManager().HasBuff(BuffType::Thorns))
                            player->TakeDamage(hitEnemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
                        CardEffect::ApplyOnHitEffect(data.onHitEffect, hitEnemy->GetBuffManager());
                        PlayOnHitVfx(data.onHitEffect, hitEnemy, gridMap);
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

            int dmg = 0;
            if (data.mainEffect.type == CardEffectType::Catalyst)
            {
                float wx = (target->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
                float wz = (target->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
                EffectManager::Play("poison_apply", wx, 0.5f, wz);

                int p = target->GetBuffManager().GetBuffValue(BuffType::Poison);
                if (p > 0)
                {
                    int add = p * (data.mainEffect.value - 1);
                    Buff b; b.type = BuffType::Poison;
                    b.value = add;
                    b.duration = add;
                    b.name = BuffInfo::Get(BuffType::Poison).name; b.description = L"";
                    target->GetBuffManager().AddBuff(b);
                }
            }
            else if (data.mainEffect.type == CardEffectType::DrainPoison)
            {
                // 対象の毒を吸収（消す）→ その value分の1 を回復
                int p = target->GetBuffManager().GetBuffValue(BuffType::Poison);
                if (p > 0)
                {
                    int heal = p / data.mainEffect.value;   // value=10→1/10, 強化5→1/5
                    if (heal < 1) heal = 1;                  // 毒があれば最低1回復
                    player->Heal(heal);
                    target->GetBuffManager().RemoveBuff(BuffType::Poison);   // 吸収＝消費
                }
                float wx = (target->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
                float wz = (target->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
                EffectManager::Play("poison_apply", wx, 0.5f, wz);
            }
            else if (data.mainEffect.type == CardEffectType::PoisonBurst)
            {
                // 毒を付与 → その毒ぶんを即座にダメージ（毒は残って継続）
                Buff b; b.type = BuffType::Poison;
                b.value = data.mainEffect.value + RelicManager::SumValue("poisonAdd");
                b.duration = b.value;
                b.name = BuffInfo::Get(BuffType::Poison).name; b.description = L"";
                target->GetBuffManager().AddBuff(b);
                int total = target->GetBuffManager().GetBuffValue(BuffType::Poison);   // 即ダメも+1込み
                target->TakeDamage(total, DamageFeel::Poison);   // 今ある毒の総量ぶん即ダメージ

                float wx = (target->gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
                float wz = (target->gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
                EffectManager::Play("poison_apply", wx, 0.5f, wz);
            }
            else if (data.mainEffect.type == CardEffectType::BlockDamage)
            {
                dmg = player->GetBlock() * data.mainEffect.value / 100;   // ブロック×value%
                if (dmg > 0) target->TakeDamage(dmg);                     // ブロックは残す
            }
            else
            {
                dmg = player->GetBuffManager().GetFinalAttack(data.mainEffect.value);

                // 背水：低HPで攻撃UP（Powerのバフ値ぶん・今のHPで判定）
                int hp = player->GetHp(), mhp = player->GetMaxHp();
                if (hp * 2 <= mhp) dmg += player->GetBuffManager().GetBuffValue(BuffType::LastStand);  // 半分以下
                if (hp * 4 <= mhp) dmg += player->GetBuffManager().GetBuffValue(BuffType::DeepStand);  // 1/4以下

                if (std::find(data.tags.begin(), data.tags.end(), "Knife") != data.tags.end())
                    dmg += player->GetBuffManager().GetBuffValue(BuffType::KnifePower)
                    + RelicManager::SumValue("knifeBonus");   // 研ぎ師

                target->TakeDamage(dmg);
            }

            int totalHits = EffectiveHits(data);
            if (totalHits > 1)
            {
                result.multiHitTargets = { target };   // ← 1要素のリスト
                result.multiHitRemain = totalHits - 1;
                result.multiHitDamage = dmg;
            }
            // Thorns反射
            if (target->GetBuffManager().HasBuff(BuffType::Thorns))
                player->TakeDamage(target->GetBuffManager().GetBuffValue(BuffType::Thorns));
            CardEffect::ApplyOnHitEffect(data.onHitEffect, target->GetBuffManager());
            CardEffect::ApplyOnHitEffect(data.onHitEffect2, target->GetBuffManager());
            PlayOnHitVfx(data.onHitEffect, target, gridMap); 
            PlayOnHitVfx(data.onHitEffect2, target, gridMap);

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
                 int moveRange = player->GetMoveRange(data.range);

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
        // 設置カード：エナジー消費前に対象セルを検証
        if (data.mainEffect.type == CardEffectType::PlaceTrap)
        {
            if (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
                data.rangeType, data.range))
                return result;
            if (targetCol < 0 || targetCol >= gridMap->GetCols() ||
                targetRow < 0 || targetRow >= gridMap->GetRows())
                return result;
            auto& cell = gridMap->GetCell(targetCol, targetRow);
            if (cell.type != CellType::Empty || cell.tileEffect.active)
                return result;
        }

        if (data.mainEffect.type == CardEffectType::PlaceTrap ||
            data.mainEffect.type == CardEffectType::PlaceTrapArea)
        {
            for (int i = 0; i < RelicManager::SumValue("trapDraw"); i++)
            {
                std::string id = deck.DrawCard();
                if (!id.empty()) { hand.AddCard(id); result.drawnCards.push_back(id); }
            }
        }

        if (data.mainEffect.type == CardEffectType::Detonate)
        {
            bool any = false;
            for (int r = 0; r < gridMap->GetRows() && !any; r++)
                for (int c = 0; c < gridMap->GetCols() && !any; c++)
                    if (gridMap->GetCell(c, r).tileEffect.active &&
                        RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range))
                        any = true;
            if (!any) return result;   // 範囲内に罠が無ければ不発（ノーコスト）
        }

        if (data.mainEffect.type == CardEffectType::DetonateAt)
        {
            if (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
                data.rangeType, data.range))
                return result;
            if (!gridMap->GetCell(targetCol, targetRow).tileEffect.active)
                return result;   // 罠が無いマスは不可（ノーコスト）
        }
        if (data.mainEffect.type == CardEffectType::DetonateChain)
        {
            if (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
                data.rangeType, data.range)) return result;
            if (!gridMap->GetCell(targetCol, targetRow).tileEffect.active) return result;
        }

        player->UseEnergy(data.cost);

        switch (data.mainEffect.type)
        {
        case CardEffectType::Block:
        {
            int gained = player->GetBuffManager().GetFinalBlock(data.mainEffect.value);
            player->AddBlock(gained);
            if (gained > 0) EffectManager::Play("block_gain", player->worldX, player->worldY + 0.4f, player->worldZ);
            break;
        }
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
            auto& cell = gridMap->GetCell(targetCol, targetRow);
            cell.tileEffect.active = true;
            cell.tileEffect.id = data.mainEffect.trapType;
            cell.tileEffect.value = data.mainEffect.value;
            cell.tileEffect.duration = data.mainEffect.duration;
            const TerrainDef* tDef = TerrainDataBase::Get(data.mainEffect.trapType);
            if (tDef) cell.tileEffect.persistent = tDef->persistent;
            break;
        }
        case CardEffectType::PlaceTrapArea:
        {
            for (int r = 0; r < gridMap->GetRows(); r++)
                for (int c = 0; c < gridMap->GetCols(); c++)
                {
                    if (!RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range)) continue;
                    Cell& cell = gridMap->GetCell(c, r);
                    if (cell.type != CellType::Empty || cell.tileEffect.active) continue;
                    cell.tileEffect.active = true;
                    cell.tileEffect.id = data.mainEffect.trapType;
                    cell.tileEffect.value = data.mainEffect.value;
                    cell.tileEffect.duration = data.mainEffect.duration;
                    const TerrainDef* tDef = TerrainDataBase::Get(data.mainEffect.trapType);
                    if (tDef) cell.tileEffect.persistent = tDef->persistent;
                }
            break;
        }
        case CardEffectType::PlaceDecoy:
        {
            if (!RangeShape::Contains(playerCol, playerRow, targetCol, targetRow,
                data.rangeType, data.range)) return result;
            if (gridMap->GetCell(targetCol, targetRow).type != CellType::Empty) return result;
            result.placeDecoy = true;
            result.decoyCol = targetCol;
            result.decoyRow = targetRow;
            break;
        }
        case CardEffectType::Detonate:
        {
            bool full = (data.mainEffect.value > 0);   // 強化で value>0 → 全開
            for (int r = 0; r < gridMap->GetRows(); r++)
                for (int c = 0; c < gridMap->GetCols(); c++)
                {
                    Cell& cell = gridMap->GetCell(c, r);
                    if (!cell.tileEffect.active) continue;
                    if (RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range))
                        CardExecutor::DetonateTrap(cell, c, r, gridMap, enemies, full);
                }
            break;
        }
        case CardEffectType::RecallTraps:
        {
            int recalled = 0;
            for (int r = 0; r < gridMap->GetRows(); r++)
                for (int c = 0; c < gridMap->GetCols(); c++)
                {
                    Cell& cell = gridMap->GetCell(c, r);
                    if (cell.tileEffect.active)
                    {
                        cell.tileEffect = TileEffect();   // 削除（起爆せず消す）
                        recalled++;
                    }
                }
            player->AddEnergy(recalled * data.mainEffect.value);
            for (int i = 0; i < recalled * data.mainEffect.value; i++)
            {
                std::string id = deck.DrawCard();
                if (!id.empty()) { hand.AddCard(id); result.drawnCards.push_back(id); }
            }
            break;
        }
        case CardEffectType::DetonateAt:
        {
            bool full = (data.mainEffect.value > 0);
            Cell& cell = gridMap->GetCell(targetCol, targetRow);
            CardExecutor::DetonateTrap(cell, targetCol, targetRow, gridMap, enemies, full);
            break;
        }
        case CardEffectType::DetonateChain:
        {
            result.startChainDetonate = true;
            result.chainCol = targetCol;
            result.chainRow = targetRow;
            result.chainFull = (data.mainEffect.value > 0);
            break;   // 実際の起爆はBattleSceneが時間差で行う
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
        ApplyAllEnemyEffect(data, enemies);
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
        {
            int gainedS = player->GetBuffManager().GetFinalBlock(data.subEffect.value);
            player->AddBlock(gainedS);
            if (gainedS > 0) EffectManager::Play("block_gain", player->worldX, player->worldY + 0.4f, player->worldZ);
            break;
        }
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

    ApplyAllEnemyEffect(data, enemies);

    if (data.exhaust)
        deck.ExhaustCard(cardId);
    else
        deck.DiscardCard(cardId);

    hand.RemoveCard(cardIndex);
    result.pendingDiscard = pendingDiscard;      // 選択はシーン側に任せる
    result.success = true;
    if (data.selfDamage > 0)
        player->LoseHp(data.selfDamage);
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

    float wx = (col - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - gridMap->GetRows() / 2.0f) * 1.1f;
    std::string fx = def->vfx.empty() ? "explosion" : def->vfx;
    EffectManager::Play(fx, wx, 0.5f, wz);

    if (def->effect == "Damage")
    {
        enemy->TakeDamage(cell.tileEffect.value + RelicManager::SumValue("trapDamage"));

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
        debuff.value = cell.tileEffect.value + RelicManager::SumValue("trapDebuff");   // ←毒等の値
        debuff.duration = def->buffDuration + RelicManager::SumValue("trapDebuff");   // ←二値の持続
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

bool CardExecutor::DetonateTrap(Cell& cell, int col, int row,
    GridMap* gridMap, std::vector<Enemy*>& enemies, bool fullPower, bool chain)
{
    if (!cell.tileEffect.active) return false;
    const TerrainDef* def = TerrainDataBase::Get(cell.tileEffect.id);
    if (!def) return false;

    // 演出：爆発エフェクト＋揺れ
    float wx = (col - gridMap->GetCols() / 2.0f) * 1.1f;
    float wz = (row - gridMap->GetRows() / 2.0f) * 1.1f;
    std::string fx = def->vfx.empty() ? "explosion" : def->vfx;
    EffectManager::Play(fx, wx, 0.5f, wz);
    ScreenShake::Add(0.3f);

    for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++)
        {
            int nc = col + dc, nr = row + dr;
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            Enemy* e = GetEnemyAt(nc, nr, enemies);
            if (!e) continue;

            bool center = (dc == 0 && dr == 0);
            int val = cell.tileEffect.value;
            if (!center && !fullPower) val /= 2;          // 周囲は半分（強化で全開）

            if (def->effect == "Damage")
                e->TakeDamage(val + RelicManager::SumValue("trapDamage"));                
            else if (def->effect == "ApplyDebuff")
            {
                Buff b; b.type = StringToBuffType(def->buffType);
                b.value = val + RelicManager::SumValue("trapDebuff");
                b.duration = def->buffDuration + RelicManager::SumValue("trapDebuff");
                b.name = def->name; b.description = L"";
                e->GetBuffManager().AddBuff(b);
            }
        }
    cell.tileEffect = TileEffect();

    if (chain)
    {
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
            {
                if (dc == 0 && dr == 0) continue;
                int nc = col + dc, nr = row + dr;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
                Cell& nx = gridMap->GetCell(nc, nr);
                if (nx.tileEffect.active)
                    DetonateTrap(nx, nc, nr, gridMap, enemies, fullPower, true);  // 再帰
            }
    }
    return true;
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
            // 物理ドミノ：勢いを1減らして相手も吹き飛ばす（連鎖）
            if (remaining - 1 > 0)
                ApplyKnockback(blocker, playerCol, playerRow, remaining - 1, gridMap, enemies);
            break;
        }

        // 移動実行
        gridMap->SetCellType(target->gridCol, target->gridRow, CellType::Empty);
        target->gridCol = nextCol;
        target->gridRow = nextRow;
        gridMap->SetCellType(nextCol, nextRow, CellType::Enemy);
        path.push_back({ (nextCol - gridMap->GetCols() / 2.0f) * 1.1f,
                          (nextRow - gridMap->GetRows() / 2.0f) * 1.1f });
        if (!path.empty())
        {
            float os = 0.35f;                 // 行き過ぎ量（マス比）
            auto fin = path.back();
            path.push_back({ fin.first + dirC * os * 1.1f, fin.second + dirR * os * 1.1f }); // 行き過ぎ
            path.push_back(fin);                                                             // 戻って着地
            target->StartWalk(path, 0.05f);   // 速く（吹っ飛び）
            ScreenShake::Add(0.4f);           // 手応え
        }
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
    std::vector<std::pair<float, float>> path;   // 通過するワールド座標（アニメ用）
    for (int i = 0; i < distance; i++)
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
        path.push_back({ (nextCol - gridMap->GetCols() / 2.0f) * 1.1f,
                                 (nextRow - gridMap->GetRows() / 2.0f) * 1.1f });

        auto& passedCell = gridMap->GetCell(nextCol, nextRow);
        TriggerTrap(passedCell, target, nextCol, nextRow, gridMap, enemies);
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