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

// 敵の出入り（出す・呼ぶ・倒れた敵の片付け）

void BattleScene::AddEnemy(int col, int row, const std::string& id)
{
    auto enemy = new Enemy();
    enemy->Init(id);
    enemy->gridCol = col;
    enemy->gridRow = row;
    enemy->BindGrid(m_gridMap, m_gridMap->GetCols(), m_gridMap->GetRows());

    enemy->CenterWorld(col, row, m_gridMap, enemy->worldX, enemy->worldZ);   // 形の中心に置く

    // 占有マスをセット
    CellType cellType = enemy->IsBoss() ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : enemy->GetGridShape())
        m_gridMap->SetCellType(col + dc, row + dr, cellType);

    if (enemy->IsSnake())
    {
        enemy->InitSnake(m_gridMap);
        enemy->GetBuffManager().AddBuff({ BuffType::HeadWeak, 2, -1 });   // 頭弱点マーク（表示用・永続）
        enemy->GetBuffManager().AddBuff({ BuffType::Molt, Enemy::SHED_DAMAGE, -1 });   // 脱皮までの残り（表示用）
    }
    if (enemy->IsImmovable())
        enemy->GetBuffManager().AddBuff({ BuffType::Immovable, 0, -1 });   // 移動不可マーク（表示用・永続）

    if (id == "l3_sovereign")
    {
        enemy->SetInvulnerable(true);   // 初手決定前から無敵に
        for (auto& c : BOSS_CHAINS) enemy->AddOverlay(c.chain);
    }

    // 識別色：今いる敵が使っていない一番小さい番号（誰かが倒れても他の敵の色は変わらない）
    int ci = 0;
    while (std::any_of(m_enemies.begin(), m_enemies.end(),
        [ci](const Enemy* e) { return e->colorIndex == ci; }))
        ci++;
    enemy->colorIndex = ci;
    enemy->hueColor = HighlightPalette::EnemyHue(ci);

    m_enemies.push_back(enemy);
}

void BattleScene::SummonNear(Enemy* src, const std::string& id, int count)
{
    if (!src->SummonPlan().empty())                                        // 予告した場所に出す
    {
        for (auto& [c, r] : src->SummonPlan())
        {
            if (m_gridMap->GetCell(c, r).type != CellType::Empty) continue;   // 塞がれたら出ない
            AddEnemy(c, r, id);
            m_enemies.back()->MarkJustSummoned();
            EffectManager::Play("summon", m_enemies.back()->worldX, 0.4f, m_enemies.back()->worldZ);
        }
        src->ClearSummonPlan();
        return;
    }

    int placed = 0;
    // 召喚主の周囲を近い順（外周リング）に探索。敵/ボス/障害のマスは飛ばして隣の空きへ
    for (int r = 1; r <= 5 && placed < count; r++)
        for (int dr = -r; dr <= r && placed < count; dr++)
            for (int dc = -r; dc <= r && placed < count; dc++)
            {
                if (abs(dc) != r && abs(dr) != r) continue;   // 外周リングだけ
                int c = src->gridCol + dc, rr = src->gridRow + dr;
                if (c < 0 || c >= m_gridMap->GetCols() || rr < 0 || rr >= m_gridMap->GetRows()) continue;
                if (m_gridMap->GetCell(c, rr).type != CellType::Empty) continue;   // 敵がいるマス等は飛ばす
                if (c == m_player->gridCol && rr == m_player->gridRow) continue;    // プレイヤーの現在地も避ける
                AddEnemy(c, rr, id);
                m_enemies.back()->MarkJustSummoned();                              // 同ターンは動かない
                EffectManager::Play("summon", m_enemies.back()->worldX, 0.4f, m_enemies.back()->worldZ);  // 召喚エフェクト
                placed++;
            }
}

void BattleScene::ProcessDeadEnemies()
{
    std::vector<Enemy*> toDelete;
    for (auto enemy : m_enemies)
        if (enemy->IsDeathFinished()) 
            toDelete.push_back(enemy);

    if (toDelete.empty()) return;   // 何も死んでなければ何もしない

    // 選択中の敵を覚えておく（死んでいたら解除）
    Enemy* selected = nullptr;
    if (m_selectedEnemyRange >= 0 && m_selectedEnemyRange < (int)m_enemies.size())
    {
        selected = m_enemies[m_selectedEnemyRange];
        for (auto d : toDelete)
            if (d == selected) { selected = nullptr; break; }
    }


    for (auto d : toDelete)   // 引き寄せの「つなぐ物」の根元の敵が消えるなら、先に戻しておく
        if (d == m_tetherEnemy) { EffectManager::Stop(m_tetherHandle, 0.15f); m_tetherHandle = 0; m_tetherEnemy = nullptr; }

    for (auto enemy : toDelete)
    {
        for (auto& [dc, dr] : enemy->GetGridShape())
            m_gridMap->SetCellType(
                enemy->gridCol + dc,
                enemy->gridRow + dr,
                CellType::Empty);

        m_enemies.erase(
            std::remove(m_enemies.begin(), m_enemies.end(), enemy),
            m_enemies.end()
        );

        m_defeatedEnemyIds.push_back(enemy->GetId());

        PlayerDataManager::AddGold(RelicManager::SumValue("killGold"));   // レリック
        m_player->Heal(RelicManager::SumValue("killHeal") + m_player->GetBuffManager().GetBuffValue(BuffType::BloodThirst));   // 血の渇望も                        // レリック
        if (int ks = RelicManager::SumValue("killStrength"))                       // レリック（撃破で攻撃UP）
        {
            Buff b; b.type = BuffType::AttackUp; b.value = ks; b.duration = -1;
            b.name = L""; b.description = L"";
            m_player->GetBuffManager().AddBuff(b);
        }
        // 毒の伝染レリック：毒持ちが倒れたら周囲の敵へ同量の毒を分散
        if (RelicManager::HasKind("poisonSpread"))
        {
            int pz = enemy->GetBuffManager().GetBuffValue(BuffType::Poison);
            if (pz > 0)
            {
                for (auto other : m_enemies)   // 死んだ敵はerase済み＝生存のみ対象
                {
                    if (other->GetHp() <= 0) continue;   // 同時に死んだ敵は除外
                    int dc = abs(other->gridCol - enemy->gridCol);
                    int dr = abs(other->gridRow - enemy->gridRow);
                    if (dc > 1 || dr > 1) continue;      // 周囲8マスのみ

                    Buff pb; pb.type = BuffType::Poison; pb.value = pz; pb.duration = pz;
                    pb.name = BuffInfo::Get(BuffType::Poison).name; pb.description = L"";
                    other->GetBuffManager().AddBuff(pb);

                    float wx = (other->gridCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                    float wz = (other->gridRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                    EffectManager::Play("poison_apply", wx, 0.5f, wz);
                }
            }
        }

        delete enemy;
    }

    // 削除後に選択中の敵の新しいindexを探し直す
    m_selectedEnemyRange = -1;
    if (selected)
    {
        for (int i = 0; i < (int)m_enemies.size(); i++)
            if (m_enemies[i] == selected) { m_selectedEnemyRange = i; break; }
    }
}

// エンカウントの敵を並べる（指名戦があればそれ）。3層ボスの楔の四隅を覚え、あふれた分だけ強さを上げる
void BattleScene::SpawnEncounter()
{
    int layer = PlayerDataManager::GetData().layer;
    const EncounterData* encounter = EncounterDataBase::GetById(m_battleEnemyId);   // 指名戦（あれば）
    if (!encounter)
        encounter = EncounterDataBase::GetEncounter(layer, m_category, m_battleTier, m_battleSeed);
    if (encounter)
    {
        for (auto& ee : encounter->enemies)
            AddEnemy(ee.col, ee.row, ee.id);

        // 3層ボス：楔の四隅スロットを記録
        for (auto e : m_enemies)
            if (e->GetId().rfind("wedge_", 0) == 0)
                m_wedgeCorners.push_back({ e->gridCol, e->gridRow });

        const auto& ladder = encounter->escalation.empty()
            ? EncounterDataBase::DefaultEscalation()
            : encounter->escalation;

        float hpMul = 1.0f, dmgMul = 1.0f;
        int bonusActions = 0;
        int level = m_overflow;
        for (int i = 0; i < level; i++)
        {
            if (i < (int)ladder.size())
            {
                const EscalationTier& t = ladder[i];
                switch (t.kind)
                {
                case EscalationKind::HpUp:      hpMul += t.value / 100.0f;   break;
                case EscalationKind::AtkUp:     dmgMul += t.value / 100.0f;   break;
                case EscalationKind::AddAction: bonusActions += 1;            break;
                case EscalationKind::AddEnemy:  AddEnemy(t.col, t.row, t.id); break;
                }
            }
            else   // 梯子超過分は緩やかに
            {
                hpMul += 0.10f;
                dmgMul += 0.10f;
            }
        }
        for (auto enemy : m_enemies)
            enemy->ApplyDifficulty(hpMul, dmgMul, bonusActions);
    }
}

// 全部の敵の次の行動を決める（デコイがあればそちらを狙う。味方の数も渡す）。戦闘開始とターン開始で共通
void BattleScene::DecideEnemyActions()
{
    for (auto enemy : m_enemies)
    {
        int tC = (m_decoyCol >= 0) ? m_decoyCol : m_playerCol;
        int tR = (m_decoyCol >= 0) ? m_decoyRow : m_playerRow;
        int allies = 0; for (auto o : m_enemies) if (o != enemy && o->GetHp() > 0) allies++;
        enemy->SetAllyCount(allies);
        enemy->DecideNextAction(tC, tR, m_turnCount);
    }
}

// HP0の敵を「塵化」させ、アニメが終わったら消す
void BattleScene::UpdateDyingEnemies(float deltaTime)
{
    for (auto enemy : m_enemies)
    {
        if (enemy->GetHp() <= 0 && !enemy->IsDying())
        {
            for (auto& [dc, dr] : enemy->GetGridShape())      // マスを即解放
                m_gridMap->SetCellType(enemy->gridCol + dc, enemy->gridRow + dr, CellType::Empty);
            enemy->StartDeath();
        }
        enemy->UpdateDeath(deltaTime);
    }
    ProcessDeadEnemies();
}
