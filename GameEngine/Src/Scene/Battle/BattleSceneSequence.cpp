#include "CardEffect.h"
#include "BattleScene.h"
#include "CardTooltip.h"
#include "TextureLoader.h"
#include "EffectManager.h"
#include "RelicManager.h"
#include "CardExecutor.h"
#include "TerrainDataBase.h"
#include "MaterialDataBase.h"
#include "HighlightPalette.h"
#include "FloatingText.h"
#include "BuffPopup.h"
#include "PileFx.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <random>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// 時間を置いて1つずつ進める処理（連撃の追撃・敵の多段・罠の連鎖など）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// カードを使った後の順番待ちを始める（カードを使う経路が2つあるので共通に）
void BattleScene::StartCardSequences(const CardExecutor::ExecuteResult& r)
{
    if (r.startChainDetonate)
        m_chain.Start({ ChainWave{ { { r.chainCol, r.chainRow } }, r.chainFull } });
    if (r.startSeqDetonate)
    {
        std::vector<TrapFire> fires;
        for (auto& [c, rr] : r.seqDetonateCells) fires.push_back({ c, rr, r.seqFull });
        m_detonate.Start(std::move(fires));   // すぐ1個目
    }
    if (r.cardUsed && r.multiHitRemain > 0)
        m_playerHits.Start(std::vector<PlayerHit>(r.multiHitRemain,
            PlayerHit{ r.multiHitTargets, r.multiHitDamage, r.multiHitOnHit, r.multiHitOnHit2 }), MULTI_HIT_INTERVAL);
}

// 自分の連撃の追撃1発
float BattleScene::StepPlayerHit(PlayerHit& h)
{
    bool anyAlive = false;
    for (auto t : h.targets)
    {
        bool alive = false;
        for (auto e : m_enemies) if (e == t && e->GetHp() > 0) alive = true;   // もう消えた敵には触らない
        if (!alive) continue;
        t->ReuseHitPart(); t->TakeDamage(h.damage); t->StartJump(0.5f, 0.2f); anyAlive = true;
        CardEffect::ApplyOnHitEffect(h.onHit, t->GetBuffManager());    // 命中時の効果もヒットごとに（弱体1×2回＝弱体2）
        CardEffect::ApplyOnHitEffect(h.onHit2, t->GetBuffManager());
    }
    return anyAlive ? MULTI_HIT_INTERVAL : -1.0f;   // 全員倒れたら残りは打たない
}

// 敵の多段攻撃の1発
float BattleScene::StepEnemyHit(EnemyHit& h)
{
    int hpBefore = m_player->GetHp(), blockBefore = m_player->GetBlock();
    XMFLOAT3 from = h.enemy ? XMFLOAT3(h.enemy->worldX, 0.0f, h.enemy->worldZ) : XMFLOAT3(m_player->worldX, 0.0f, m_player->worldZ + 1.0f);
    m_player->TakeDamage(h.damage, DamageFeel::Hit, &from);   // 1発（数字はTakeDamage内で出る）。盾は攻撃してきた敵の方へ
    if (!h.vfx.empty()) PlayEnemyHitFx(h.vfx);
    bool fullyBlocked = (m_player->GetHp() == hpBefore);
    int blocked = blockBefore - m_player->GetBlock();   // ブロックで防いだ量
    if (h.enemy && h.enemy->GetHp() > 0)
    {
        int th = m_player->GetBuffManager().GetBuffValue(BuffType::Thorns);
        if (th > 0) h.enemy->TakeDamage(th);
        int rip = m_player->GetBuffManager().GetBuffValue(BuffType::Riposte);
        if (blocked > 0 && rip > 0) h.enemy->TakeDamage(blocked * rip / 100);   // 受け返し：防いだダメージの rip% を返す
    }
    // 最後の1発の後に状態異常（単発と同じく、完全に防いだら付かない。ブロックは前の発から減るので＝1発でも通ったら付く）
    if (h.last && h.enemy)
    {
        if (!fullyBlocked)
            for (auto& b : h.enemy->PendingPlayerBuffs())
                m_player->GetBuffManager().AddBuff(b);
        h.enemy->PendingPlayerBuffs().clear();
    }
    return MULTI_HIT_INTERVAL;
}

// 大蛇のとぐろ：通ったマスの罠を1つ
float BattleScene::StepCoilTrap(CoilTrap& t)
{
    auto& cell = m_gridMap->GetCell(t.c, t.r);
    if (cell.tileEffect.active && t.enemy->GetHp() > 0)
        CardExecutor::TriggerTrap(cell, t.enemy, t.c, t.r, m_gridMap, m_enemies);
    return 0.15f;
}

// レリック：戦闘開始時に全敵へ毒（1回だけ）
float BattleScene::StepStartPoison(int& amount)
{
    for (auto enemy : m_enemies)
    {
        if (enemy->GetHp() <= 0) continue;
        Buff b; b.type = BuffType::Poison;
        b.value = amount + RelicManager::SumValue("poisonAdd");
        b.duration = b.value;
        b.name = BuffInfo::Get(BuffType::Poison).name; b.description = L"";
        enemy->GetBuffManager().AddBuff(b);
        float wx = (enemy->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (enemy->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
        EffectManager::Play("poison_apply", wx, 0.5f, wz);
    }
    return -1.0f;
}

// 連鎖起爆：今の波を起爆し、隣の罠を次の波に
float BattleScene::StepChainWave(ChainWave& w)
{
    ChainWave next{ {}, w.full };
    for (auto& [c, r] : w.cells)
    {
        auto& cell = m_gridMap->GetCell(c, r);
        if (!cell.tileEffect.active || cell.tileEffect.enemyOwned) continue;   // 敵床は除外
        CardExecutor::DetonateTrap(cell, c, r, m_gridMap, m_enemies, w.full, false);
        for (int dr = -1; dr <= 1; dr++)
            for (int dc = -1; dc <= 1; dc++)
            {
                if (dc == 0 && dr == 0) continue;
                int nc = c + dc, nr = r + dr;
                if (nc < 0 || nc >= m_gridMap->GetCols() || nr < 0 || nr >= m_gridMap->GetRows()) continue;
                auto& ncell = m_gridMap->GetCell(nc, nr);
                if (ncell.tileEffect.active && !ncell.tileEffect.enemyOwned)   // 敵床に連鎖しない
                    next.cells.push_back({ nc, nr });
            }
    }
    if (!next.cells.empty()) m_chain.Push(next);
    ScreenShake::Add(0.3f);
    ProcessDeadEnemies();
    return 0.12f;   // 波の間隔（大きいほどゆっくり連鎖）
}

// 一斉起爆・導火線：罠を1つ
float BattleScene::StepDetonate(TrapFire& f)
{
    auto& cell = m_gridMap->GetCell(f.c, f.r);
    if (cell.tileEffect.active)
    {
        CardExecutor::DetonateTrap(cell, f.c, f.r, m_gridMap, m_enemies, f.full, false);
        ScreenShake::Add(0.2f);
        ProcessDeadEnemies();
    }
    return 0.08f;   // 1個ずつの間隔（大きいほどゆっくり）
}

// 時間を置いて1つずつ進める処理（どれも同じ形：1件処理して、返した秒数だけ待つ。中身は Step〜）
void BattleScene::UpdateSequences(float deltaTime)
{
    m_playerHits.Update(deltaTime,  [&](PlayerHit& h) { return StepPlayerHit(h); });
    m_enemyHits.Update(deltaTime,   [&](EnemyHit& h)  { return StepEnemyHit(h); });
    m_coilTraps.Update(deltaTime,   [&](CoilTrap& c)  { return StepCoilTrap(c); });
    m_startPoison.Update(deltaTime, [&](int& amount)  { return StepStartPoison(amount); });
    m_chain.Update(deltaTime,       [&](ChainWave& w) { return StepChainWave(w); });
    m_detonate.Update(deltaTime,    [&](TrapFire& f)  { return StepDetonate(f); });
}
