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

// 敵のターン（段階ごとの関数。敵1体の行動は EnemyPhaseAct）
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

// 敵のターン：段階ごとに呼び分ける
void BattleScene::UpdateEnemyTurn(float deltaTime)
{
    m_highlighter.ClearPlayerHighlight(m_gridMap);
    m_highlighter.ClearEnemyHighlight(m_gridMap);

    switch (m_enemyPhase)
    {
    case EnemyTurnPhase::WaitStart:    EnemyPhaseWaitStart(deltaTime);  break;
    case EnemyTurnPhase::PoisonTick:   EnemyPhasePoison(deltaTime);     break;
    case EnemyTurnPhase::ProcessEnemy: EnemyPhaseAct(deltaTime);        break;
    case EnemyTurnPhase::WaitAction:   EnemyPhaseWaitAction(deltaTime); break;
    case EnemyTurnPhase::NextEnemy:    EnemyPhaseNext(deltaTime);       break;
    case EnemyTurnPhase::EndTurn:      EnemyPhaseEnd();                 break;
    case EnemyTurnPhase::DashStep:     EnemyPhaseDash();                break;
    }
}

// 敵のターンの始め：少し待つ（過負荷の演出が終わるまで）
void BattleScene::EnemyPhaseWaitStart(float deltaTime)
{
    m_enemyActionDelay -= deltaTime;
    if (m_enemyActionDelay <= 0)
    {
        if (m_enemies.empty())
            m_enemyPhase = EnemyTurnPhase::EndTurn;
        else
            m_enemyPhase = EnemyTurnPhase::PoisonTick;
    }
}

// 敵の毒：1体ずつ、脈動の分だけ連続でダメージ
void BattleScene::EnemyPhasePoison(float deltaTime)
{
    m_enemyActionDelay -= deltaTime;
    if (m_enemyActionDelay > 0) return;

    // 現在の敵にサブティックが残っていれば1回（連続攻撃風・少しずらす）
    if (m_poisonSubTicks > 0)
    {
        Enemy* e = (m_poisonIdx < (int)m_enemies.size()) ? m_enemies[m_poisonIdx] : nullptr;
        if (e && e->GetHp() > 0)
        {
            int pd = e->GetBuffManager().TickPoison();   // 現在の毒→ダメージ、毒-1
            if (pd > 0)
            {
                e->TakeDamage(pd, DamageFeel::Poison);
                ScreenShake::Add(0.15f);                 // 毒でも画面振動
            }
            if (e->GetHp() <= 0) m_poisonSubTicks = 1;   // 死んだら残り打ち切り
        }
        m_poisonSubTicks--;
        m_enemyActionDelay = 0.12f;                       // 連続の間隔
        if (m_poisonSubTicks <= 0) m_poisonIdx++;         // この敵完了→次へ
        return;
    }

    // 次の「生存＆毒持ち」敵を探す
    while (m_poisonIdx < (int)m_enemies.size())
    {
        Enemy* e = m_enemies[m_poisonIdx];
        if (e->GetHp() > 0 && e->GetBuffManager().GetBuffValue(BuffType::Poison) > 0) break;
        m_poisonIdx++;
    }

    if (m_poisonIdx >= (int)m_enemies.size())
    {
        ProcessDeadEnemies();
        m_currentEnemyIdx = 0;
        m_enemyPhase = m_enemies.empty()
            ? EnemyTurnPhase::EndTurn : EnemyTurnPhase::ProcessEnemy;
        return;
    }

    // この敵のサブティック数をセット（1 + 脈動）
    m_poisonSubTicks = 1 + m_player->GetBuffManager().GetBuffValue(BuffType::ToxicRhythm);
    m_enemyActionDelay = 0.0f;   // すぐ最初のティックへ
}

// 敵1体の行動
void BattleScene::EnemyPhaseAct(float deltaTime)
{
    if (m_windupTimer > 0.0f)            // 落下中は行動を保留
    {
        m_windupTimer -= deltaTime;
        if (m_windupTimer > 0.0f) return;
    }

    Enemy* enemy = NextActingEnemy();
    if (!enemy) return;
    int ai = enemy->GetActionIndex();
    int tC, tR;
    ChooseEnemyTarget(enemy, ai, tC, tR);

    const EnemyAction* wa = enemy->GetNextAction();
    if (StartEnemyWindup(enemy, wa, tC, tR)) return;   // 予備動作：着弾まで待つ
    m_windupPlayed = false;
    int windupHandle = m_windupHandle; m_windupHandle = 0;   // この行動の予備動作の演出（引き寄せならこの後も使う）

    int preCol = enemy->gridCol, preRow = enemy->gridRow;   // 実行前の位置（予告の範囲の中心。移動する攻撃でも演出は予告どおりの所に出す）
    std::vector<std::pair<int, int>> preCells = ThreatCellsBeforeAct(enemy, wa);

    // 現在の行動を実行
    bool targetedDecoy = (m_decoyCol >= 0 && tC == m_decoyCol && tR == m_decoyRow);
    bool atk = false;
    int damage = 0;
    if (IsBossAwakenTurn(enemy)) BeginBossAwaken(enemy);
    else
    {
        damage = enemy->ExecuteAction(ai, m_playerCol, m_playerRow, m_gridMap, m_player, m_enemies, tC, tR, &atk);
        if (wa && windupHandle != 0 && wa->HasEffect(EffectKind::PullPlayer))   // 引き寄せ：伸ばした物を、引っぱる間くっつけておく
        { m_tetherHandle = windupHandle; m_tetherEnemy = enemy; }
    }
    PlayEnemyActionFx(enemy, wa, tC, tR, preCol, preRow, preCells, damage);
    InsertEnemyCards(enemy);

    for (auto& sm : enemy->TakePendingSummons())
        SummonNear(enemy, sm.first, sm.second);

    m_playerCol = m_player->gridCol;
    m_playerRow = m_player->gridRow;
    bool fullyBlocked = false;
    bool multiHit = ApplyEnemyDamage(enemy, wa, damage, tC, tR, preCol, preRow, fullyBlocked);   // 多段は Update の順番待ちで1発ずつ

    // 状態異常はダメージの後に付与（多段は最後の1発の後＝StepEnemyHit で）
    if (!multiHit)
    {
        if (!fullyBlocked)
            for (auto& b : enemy->PendingPlayerBuffs())
                m_player->GetBuffManager().AddBuff(b);
        enemy->PendingPlayerBuffs().clear();
    }
    enemy->SetActionIndex(ai + 1);
    if (targetedDecoy && atk) BreakDecoy();
    if (StartEnemyDash(enemy)) return;
    TriggerTrapsOnPath(enemy);

    m_enemyPhase = EnemyTurnPhase::WaitAction;
    m_enemyActionDelay = ENEMY_ACTION_PAUSE;
}

// 次に行動する敵（倒れた敵・呼ばれたばかりの敵はとばす）。いなければターンの終わりへ進めて nullptr
Enemy* BattleScene::NextActingEnemy()
{
    while (m_currentEnemyIdx < (int)m_enemies.size()
        && (m_enemies[m_currentEnemyIdx]->GetHp() <= 0
            || m_enemies[m_currentEnemyIdx]->TakeJustSummoned()))
    {
        m_currentEnemyIdx++; m_windupPlayed = false;
    }

    if (m_currentEnemyIdx >= (int)m_enemies.size())
    {
        m_enemyPhase = EnemyTurnPhase::EndTurn;
        return nullptr;
    }
    return m_enemies[m_currentEnemyIdx];
}

// 敵が狙う先：プレイヤーかデコイの近い方（必中はデコイ優先）
void BattleScene::ChooseEnemyTarget(Enemy* enemy, int ai, int& tC, int& tR) const
{
    tC = m_playerCol; tR = m_playerRow;
    if (m_decoyCol >= 0)
    {
        if (enemy->IsActionUnavoidable(ai))
        {
            tC = m_decoyCol; tR = m_decoyRow;   // 必中はデコイ優先（近さ無視）
        }
        else
        {
            int dP = abs(enemy->gridCol - m_playerCol) + abs(enemy->gridRow - m_playerRow);
            int dD = abs(enemy->gridCol - m_decoyCol) + abs(enemy->gridRow - m_decoyRow);
            if (dD < dP) { tC = m_decoyCol; tR = m_decoyRow; }
        }
    }
}

// 予備動作：落下・タメなどの演出を出して、着弾まで実行を待つ（口・列・自分・飛び道具）。始めたら true
bool BattleScene::StartEnemyWindup(Enemy* enemy, const EnemyAction* wa, int tC, int tR)
{
    if (wa && !wa->windupVfx.empty() && wa->windup > 0.0f && !m_windupPlayed
        && (!wa->HasEffect(EffectKind::PullPlayer) || wa->target.unavoidable
            || enemy->IsThreateningCell(m_playerCol, m_playerRow, *wa)))   // 引き寄せは、当たる時だけ伸ばす
    {
        int ac, ar; enemy->GetAnchorCell(*wa, tC, tR, ac, ar);
        float wx = (ac - m_gridMap->GetCols() / 2.0f) * 1.1f;
        float wz = (ar - m_gridMap->GetRows() / 2.0f) * 1.1f;
        // 飛び道具は敵の体の真ん中から飛ばす（飛び始めを使わない演出は、今までどおり wx,0.5,wz に出る）
        XMFLOAT3 from(enemy->worldX, enemy->worldY + enemy->height * 0.5f, enemy->worldZ);
        XMFLOAT3 mouth, ln, lf; float mrot;
        if (wa->windupAt == WindupAt::Mouth && EnemyMouth(enemy, *wa, tC, tR, mouth, mrot))   // 口に集める（滅びの息吹のタメ）
            m_windupHandle = EffectManager::Play(wa->windupVfx, mouth.x, mouth.y, mouth.z, mrot);
        else if (wa->windupAt == WindupAt::Line)   // 列の端から端へ（次元十字の裂け目とビーム）。縦・横の列ごとに1本
        {
            auto playLine = [&](const TargetSpec& tg) {
                if (LineEnds(enemy, tg, tC, tR, enemy->gridCol, enemy->gridRow, ln, lf))
                    m_windupHandle = EffectManager::PlayFrom(wa->windupVfx, ln, lf);
            };
            playLine(wa->target);
            for (auto& tg : wa->extraTargets) playLine(tg);
        }
        else if (wa->windupAt == WindupAt::Self)   // 敵自身（次元崩しの吸い込み）
            m_windupHandle = EffectManager::Play(wa->windupVfx, from.x, from.y, from.z);
        else
            m_windupHandle = EffectManager::PlayFrom(wa->windupVfx, from, XMFLOAT3(wx, 0.5f, wz));
        m_windupPlayed = true;
        m_windupTimer = wa->windup;
        return true;
    }
    return false;
}

// 実行前の攻撃範囲を控える（移動を伴う攻撃でも予告と一致させる）
std::vector<std::pair<int, int>> BattleScene::ThreatCellsBeforeAct(Enemy* enemy, const EnemyAction* wa)
{
    std::vector<std::pair<int, int>> preCells;
    if (wa && !wa->target.unavoidable && wa->target.approach != ApproachType::Dash)
    {
        for (auto& ef : wa->effects)
            if (ef.kind == EffectKind::Damage || (ef.kind == EffectKind::Debuff && ef.applyTo == ApplyTo::Player))   // デバフだけの行動も範囲に演出を出す
            {
                preCells = enemy->GetThreatCells(*wa, m_gridMap);
                break;
            }
    }
    return preCells;
}

// 敵の攻撃のダメージ。多段・広がる波は順番待ちで1発ずつ（true を返す。状態異常は最後の1発の後）、単発はここで受ける
bool BattleScene::ApplyEnemyDamage(Enemy* enemy, const EnemyAction* wa, int damage, int tC, int tR, int preCol, int preRow, bool& fullyBlocked)
{
    if (damage <= 0) return false;
    float waveDelay = (wa && wa->waveTime > 0.0f) ? WaveArrival(enemy, *wa, tC, tR, preCol, preRow, CellWX(m_playerCol), CellWZ(m_playerRow)) : 0.0f;
    if (enemy->GetLastHitCount() > 1 || waveDelay > 0.0f)
    {
        // 多段：1発ずつ時間差で適用／広がる波：波がプレイヤーに届いた時に（状態異常は最後の1発の後）
        int n = (std::max)(1, enemy->GetLastHitCount());
        int each = (n > 1) ? enemy->GetLastHitDamage() : damage;
        std::vector<EnemyHit> hits(n, EnemyHit{ enemy, each, false, wa ? wa->hitVfx : std::string() });
        hits.back().last = true;
        m_enemyHits.Start(std::move(hits), waveDelay);
        return true;
    }
    else
    {
        int hpBefore = m_player->GetHp(), blockBefore = m_player->GetBlock();
        XMFLOAT3 from(enemy->worldX, 0.0f, enemy->worldZ);   // ブロックの盾を、この敵の方に向ける
        m_player->TakeDamage(damage, DamageFeel::Hit, &from);
        if (wa && !wa->hitVfx.empty()) PlayEnemyHitFx(wa->hitVfx);
        fullyBlocked = (m_player->GetHp() == hpBefore);
        int blocked = blockBefore - m_player->GetBlock();   // ブロックで防いだ量
        ApplyCounterDamage(enemy, blocked);   // トゲ・受け返し
    }
    return false;
}

// デコイを壊す（デコイを狙って攻撃した時だけ。移動だけでは壊れない）
void BattleScene::BreakDecoy()
{
    float x = (m_decoyCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
    float z = (m_decoyRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
    EffectManager::Play("decoy_break", x, 0.5f, z);
    ScreenShake::Add(0.3f);
    m_decoyCol = -1; m_decoyRow = -1;
}

// 突進：止まるマス（罠マス＋最終マス）を先に並べて、間は滑らかに移動。始めたら true
bool BattleScene::StartEnemyDash(Enemy* enemy)
{
    if (!enemy->DidDash() || enemy->GetMovePath().empty()) return false;
    const auto& path = enemy->GetMovePath();
    m_dashEnemy = enemy;
    m_dashStops.clear();
    for (auto& c : path)
        for (auto& [dc, dr] : enemy->GetGridShape())   // 体のどこかが罠に乗るマスで止まる
        {
            int tc = c.first + dc, tr = c.second + dr;
            if (tc >= 0 && tr >= 0 && tc < m_gridMap->GetCols() && tr < m_gridMap->GetRows()
                && m_gridMap->GetCell(tc, tr).tileEffect.active)
            {
                m_dashStops.push_back(c);
                break;
            }
        }
    if (m_dashStops.empty() || m_dashStops.back() != path.back())
        m_dashStops.push_back(path.back());   // 最終マスは必ず終点

    m_dashStopIdx = 0;
    m_dashEnemy->m_feel.SetDash(true);   // 土煙を出し続け、ブラーを伸ばす
    DashGlideTo(m_dashStops[0].first, m_dashStops[0].second);
    m_enemyPhase = EnemyTurnPhase::DashStep;
    return true;
}

// 行動の後：動き・連撃・波のダメージが全部終わるまで待つ。同じ敵に行動が残っていれば続ける
void BattleScene::EnemyPhaseWaitAction(float deltaTime)
{
    m_enemyActionDelay -= deltaTime;

    bool anyMoving = false;
    for (auto enemy : m_enemies)
        if (enemy->IsMoving() || enemy->IsLunging() || enemy->IsJumping()) anyMoving = true;

    if (m_enemyActionDelay <= 0 && !anyMoving && m_coilTraps.Empty() && m_enemyHits.Empty())   // 連撃・波のダメージが全部入ってから次へ
    {
        Enemy* enemy = (m_currentEnemyIdx < (int)m_enemies.size())
            ? m_enemies[m_currentEnemyIdx] : nullptr;

        // 同じ敵にまだ行動が残っていれば続けて実行
        if (enemy && enemy->GetHp() > 0 && enemy->HasMoreActions())
        {
            m_enemyPhase = EnemyTurnPhase::ProcessEnemy;
        }
        else
        {
            m_enemyPhase = EnemyTurnPhase::NextEnemy;
            m_enemyActionDelay = ENEMY_BETWEEN_PAUSE;
        }
    }
}

// 次の敵へ
void BattleScene::EnemyPhaseNext(float deltaTime)
{
    m_enemyActionDelay -= deltaTime;
    if (m_enemyActionDelay <= 0)
    {
        m_currentEnemyIdx++;
        if (m_currentEnemyIdx >= (int)m_enemies.size())
            m_enemyPhase = EnemyTurnPhase::EndTurn;
        else
            m_enemyPhase = EnemyTurnPhase::ProcessEnemy;
    }
}

// 敵のターンの終わり
void BattleScene::EnemyPhaseEnd()
{
    ProcessDeadEnemies();
    for (auto enemy : m_enemies)
        enemy->GetBuffManager().TickDurations(true);   // 敵のデバフ：敵のターンの終わりに減る（今と同じ）
    m_turnManager.EndTurn();
}

// 突進：止まるマスへ1つずつグライドし、着いたマスの罠を踏む
void BattleScene::EnemyPhaseDash()
{
    if (!m_dashEnemy) { m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE; return; }
    if (m_dashEnemy->IsMoving()) return;   // グライド中は待つ

    // 到着した stop の罠を発動（罠でなければ no-op）
    if (m_dashStopIdx < (int)m_dashStops.size())
    {
        auto s = m_dashStops[m_dashStopIdx];
        TriggerTrapsUnder(m_dashEnemy, s.first, s.second);   // 体の下の罠を全部
        m_dashStopIdx++;
    }

    // 罠で倒れたら終了
    if (m_dashEnemy->GetHp() <= 0)
    {
        m_dashEnemy->m_feel.SetDash(false);   // 突進おわり：止まった所に土煙、ブラーは縮んで消える
        m_dashEnemy = nullptr;
        m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE;
        return;
    }

    // 次の stop へグライド、なければ終了
    if (m_dashStopIdx < (int)m_dashStops.size())
        DashGlideTo(m_dashStops[m_dashStopIdx].first, m_dashStops[m_dashStopIdx].second);
    else
    {
        m_dashEnemy->m_feel.SetDash(false);   // 突進おわり：止まった所に土煙、ブラーは縮んで消える
        m_dashEnemy = nullptr;
        m_enemyPhase = EnemyTurnPhase::WaitAction; m_enemyActionDelay = ENEMY_ACTION_PAUSE;
    }
}

void BattleScene::DashGlideTo(int c, int r)
{
    if (!m_dashEnemy) return;
    float wx, wz; m_dashEnemy->CenterWorld(c, r, m_gridMap, wx, wz);   // 形の中心へ
    float dist = fabsf(wx - m_dashEnemy->worldX) + fabsf(wz - m_dashEnemy->worldZ);
    float dur = (dist / 1.1f) * 0.07f;   // 1マスあたり0.07秒＝距離で時間が伸びる＝速度一定感を避けつつ滑らか
    if (dur < 0.06f) dur = 0.06f;
    m_dashEnemy->StartMove(wx, wz, dur, false);   // false = smoothstep（緩急つき）
}
