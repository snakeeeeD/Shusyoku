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

// 攻撃カード（全体効果だけ／範囲・扇／貫通／空マスへ突進／単体）
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// 攻撃カード：全体効果だけ／範囲・扇／貫通／空マスへ突進／単体 に振り分ける
bool CardExecutor::Play::Attack()
{
    if (data.allEnemyEffect.hasEffect && !AnyEnemyInRange()) return AttackAllOnly();
    if (data.rangeType == RangeType::Area || data.rangeType == RangeType::Cone) return AttackArea();
    if (data.pierce) return AttackPierce();

    Enemy* target = GetEnemyAt(targetCol, targetRow, enemies);
    result.hitTarget = target;   // 燃やした後の追撃（生贄の刃）に使う
    if (!target) return data.dash ? DashToEmptyCell() : false;
    return AttackSingle(target);
}

// 射程の中に生きている敵がいるか（全体効果のカード用。大きい敵は形のどこか1マス）
bool CardExecutor::Play::AnyEnemyInRange() const
{
    bool anyInRange = false;
    for (auto en : enemies)
    {
        if (!en || en->GetHp() <= 0) continue;
        int adx = 0, ady = 0;
        if (data.rangeType == RangeType::Cone)
            RangeShape::CardinalAim(playerCol, playerRow, targetCol, targetRow, adx, ady);
        for (auto& [dc, dr] : en->GetGridShape())
            if (RangeShape::Contains(playerCol, playerRow,
                en->gridCol + dc, en->gridRow + dr, data.rangeType, data.range, 0, adx, ady))
            {
                anyInRange = true; break;
            }
        if (anyInRange) break;
    }
    return anyInRange;
}

// 全体効果のカード：射程に敵がいなくても、効果だけは発動して使い切る
bool CardExecutor::Play::AttackAllOnly()
{
    player->UseEnergy(payCost);
    ApplyAllEnemyEffect(data, enemies, gridMap);
    if (data.exhaust) deck.ExhaustCard(cardId);
    else              deck.DiscardCard(cardId);
    hand.RemoveCard(cardIndex);
    result.success = true;
    result.cardUsed = true;
    return false;   // 使い切った（後片付けは済み）
}

// 範囲・扇の攻撃：範囲の敵すべてに当てる（扇は狙った向き）
bool CardExecutor::Play::AttackArea()
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
        return false;
    }

    player->UseEnergy(payCost);

    int baseVal = data.mainEffect.value;
    if (data.scaleByTrapCount)
    {
        int traps = 0;
        for (int r = 0; r < gridMap->GetRows(); r++)
            for (int c = 0; c < gridMap->GetCols(); c++)
                if (gridMap->GetCell(c, r).tileEffect.active) traps++;
        baseVal *= traps;   // 盤面の罠数だけ倍
    }

    int hits = EffectiveHits(data, player);
    int dmg = CardDamage::Compute(data, *player, baseVal);   // 表示と同じ計算（罠の数を掛けた後に上乗せ）
    for (auto enemy : targets)
    {
        enemy->TakeDamage(dmg);

        if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
            player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
        CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
        PlayOnHitVfx(data.onHitEffect, enemy, gridMap);
        ApplyOnHitFaith(data.onHitEffect, enemy, gridMap);   // 命中時の刻む

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
        result.multiHitOnHit = data.onHitEffect;     // 追撃でもヒットごとに付ける
        result.multiHitOnHit2 = data.onHitEffect2;
    }
    return true;
}

// 貫通：まっすぐな列の敵すべてに当てる（壁で止まる）
bool CardExecutor::Play::AttackPierce()
{
    int dx = 0, dy = 0;
    if (targetCol > playerCol) dx = 1;
    else if (targetCol < playerCol) dx = -1;
    if (targetRow > playerRow) dy = 1;
    else if (targetRow < playerRow) dy = -1;

    if ((dx != 0 && dy != 0) || (dx == 0 && dy == 0))
        return false;

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
        return false;

    player->UseEnergy(payCost);

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
            for (int h = 0; h < EffectiveHits(data, player); h++)
                enemy->TakeDamage(CardDamage::Compute(data, *player, data.mainEffect.value));

            if (enemy->GetBuffManager().HasBuff(BuffType::Thorns))
                player->TakeDamage(enemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
            CardEffect::ApplyOnHitEffect(data.onHitEffect, enemy->GetBuffManager());
            PlayOnHitVfx(data.onHitEffect, enemy, gridMap);
            ApplyOnHitFaith(data.onHitEffect, enemy, gridMap);   // 命中時の刻む
        }
    }

    result.cardUsed = true;
    return true;
}

// 突進のカードで空いたマスを選んだ：その向きへ進み、ぶつかった敵に当てる（縦・横だけ）
bool CardExecutor::Play::DashToEmptyCell()
{
        // 方向を決定
        int dx = 0, dy = 0;
        if (targetCol > playerCol) dx = 1;
        else if (targetCol < playerCol) dx = -1;
        if (targetRow > playerRow) dy = 1;
        else if (targetRow < playerRow) dy = -1;

        // 縦か横のみ（斜め不可）
        if ((dx != 0 && dy != 0) || (dx == 0 && dy == 0))
            return false;

        player->UseEnergy(payCost);

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

            result.moveDist = moveDist;   // 移動の効果は BattleScene::OnPlayerMoved
        }

        // ダメージ
        if (hitEnemy)
        {
            for (int h = 0; h < EffectiveHits(data, player); h++)
                hitEnemy->TakeDamage(CardDamage::Compute(data, *player, data.mainEffect.value));
            if (hitEnemy->GetBuffManager().HasBuff(BuffType::Thorns))
                player->TakeDamage(hitEnemy->GetBuffManager().GetBuffValue(BuffType::Thorns));
            CardEffect::ApplyOnHitEffect(data.onHitEffect, hitEnemy->GetBuffManager());
            PlayOnHitVfx(data.onHitEffect, hitEnemy, gridMap);
            ApplyOnHitFaith(data.onHitEffect, hitEnemy, gridMap);   // 命中時の刻む
        }

        result.cardUsed = true;
    return true;
}

// 単体攻撃（射程は表示と同じ形で判定。大蛇は胴体も）
bool CardExecutor::Play::AttackSingle(Enemy* target)
{
    int range = data.range;
    if (player->GetBuffManager().HasBuff(BuffType::Reposition))
        range += player->GetBuffManager().GetBuffValue(BuffType::Reposition);
    // 表示している形状と同じ判定（敵は複数マス占有しうるので、どれか1マスでも範囲内ならOK）
    bool inShape = false;
    for (auto& [dc, dr] : target->GetGridShape())
        if (RangeShape::Contains(playerCol, playerRow,
            target->gridCol + dc, target->gridRow + dr, data.rangeType, range))
        {
            inShape = true; break;
        }
    if (!inShape && target->IsSnake())               // 胴体マスも射程判定に含める
        for (auto& b : target->GetBodyCells())
            if (RangeShape::Contains(playerCol, playerRow,
                b.first, b.second, data.rangeType, range))
            {
                inShape = true; break;
            }
    if (!inShape)
        return false;
    player->UseEnergy(payCost);
    if (data.preBlock > 0)   // 盾打ち：先にブロックを得る（ブロック分のダメージに入る）
    {
        player->AddBlock(player->GetBuffManager().GetFinalBlock(data.preBlock));
        EffectManager::Play("block_gain", player->worldX, player->worldY + 0.4f, player->worldZ);
    }

    if (data.dash && target) DashToTarget(target, range);

    int dmg = ApplySpecialAttack(target);

    int totalHits = EffectiveHits(data, player);
    if (totalHits > 1)
    {
        result.multiHitTargets = { target };   // ← 1要素のリスト
        result.multiHitRemain = totalHits - 1;
        result.multiHitDamage = dmg;
        result.multiHitOnHit = data.onHitEffect;     // 追撃でもヒットごとに付ける
        result.multiHitOnHit2 = data.onHitEffect2;
    }
    ApplyHitEffects(target);
    return true;
}

// 突進：狙ったマスの手前まで移動（蛇の体を狙うと、頭は一直線上にいるとは限らない）
void CardExecutor::Play::DashToTarget(Enemy* target, int range)
{
    int dx = 0, dy = 0;
    if (playerCol < targetCol) dx = 1;
    else if (playerCol > targetCol) dx = -1;
    if (playerRow < targetRow) dy = 1;
    else if (playerRow > targetRow) dy = -1;

    int destCol = targetCol - dx;
    int destRow = targetRow - dy;

    if ((destCol != playerCol || destRow != playerRow)
        && RangeShape::Contains(playerCol, playerRow, targetCol, targetRow, data.rangeType, range)
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
        result.moveDist = abs(destCol - playerCol) + abs(destRow - playerRow);
    }
}

// 単体攻撃の本体：毒の倍化・毒の吸収・毒の破裂・ブロック打ち・ふつうのダメージ。与えたダメージ（連撃の追撃に使う）を返す
int CardExecutor::Play::ApplySpecialAttack(Enemy* target)
{
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
        dmg = CardDamage::Compute(data, *player, data.mainEffect.value);   // 表示と同じ計算（CardDamage.h）
        target->TakeDamage(dmg);
    }
    return dmg;
}

// 単体攻撃が当たった時：トゲの反射・命中時の効果（2つ）・演出・刻む・ノックバック／引き寄せ
void CardExecutor::Play::ApplyHitEffects(Enemy* target)
{
    // Thorns反射
    if (target->GetBuffManager().HasBuff(BuffType::Thorns))
        player->TakeDamage(target->GetBuffManager().GetBuffValue(BuffType::Thorns));
    CardEffect::ApplyOnHitEffect(data.onHitEffect, target->GetBuffManager());
    CardEffect::ApplyOnHitEffect(data.onHitEffect2, target->GetBuffManager());
    PlayOnHitVfx(data.onHitEffect, target, gridMap);
    ApplyOnHitFaith(data.onHitEffect, target, gridMap);   // 命中時の刻む
    PlayOnHitVfx(data.onHitEffect2, target, gridMap);
    ApplyOnHitFaith(data.onHitEffect2, target, gridMap);   // 命中時の刻む

    // ノックバック/引き寄せ
    if (data.onHitEffect.hasEffect)
    {
        if (data.onHitEffect.type == CardEffectType::Knockback)
            ApplyKnockback(target, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies);
        else if (data.onHitEffect.type == CardEffectType::Pull)
            ApplyPull(target, playerCol, playerRow, data.onHitEffect.value, gridMap, enemies, outNewPlayerCol, outNewPlayerRow);
    }
}
