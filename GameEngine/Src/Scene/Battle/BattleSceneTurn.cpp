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

// ターンの流れ（配り終わり・ターン開始の追加ドロー・説明を閉じた時）

void BattleScene::RunTurnCycle()
{
    int n = RelicManager::SumValue("turnCycle");
    if (n <= 0) return;

    if (m_holdForTutorial) { m_turnCyclePending = true; return; }   // 説明中は出さない（閉じたら発動）

    // 追加で n 枚引く（7→8枚）
    for (int i = 0; i < n; i++)
    {
        std::string id = m_deck.DrawCard();
        if (!id.empty()) { m_hand.AddCard(id); m_battleUI->StartDrawCardEffect(id); }
    }

    // n 枚を「選んで捨てる」既存UIを起動（8→7枚、選んだカードのonDiscardが発動）
    m_discardSelectCount = min(n, (int)m_hand.GetCards().size());
}

void BattleScene::OnTutorialClosed()
{
    if (!m_holdForTutorial) return;
    m_holdForTutorial = false;
    if (m_turnCyclePending) { m_turnCyclePending = false; RunTurnCycle(); }
}

void BattleScene::StartDrawSequence(int count)
{
    m_drawSeqRemaining = count;
    m_drawSeqTimer = 0.0f;   // 最初の1枚はすぐ
    m_reshuffleFxTimer = 0.0f;
}

void BattleScene::OnDrawSequenceComplete()
{
    // 刃の心得：ナイフ生成（ドロー完了後に手札へ）
    int kg = m_player->GetBuffManager().GetBuffValue(BuffType::KnifeGen);
    for (int i = 0; i < kg; i++)
    {
        m_hand.AddCard("ATK_knife");
        m_battleUI->StartDrawCardEffect("ATK_knife");
    }
    // 灰の王冠（代償）：引き終わったら、手札をランダムに n 枚燃やす（燃やすと得をする物もここで発動）
    if (int n = RelicManager::SumValue("turnBurn"); n > 0) BurnRandomCards(n);
    RunTurnCycle();
}

// プレイヤーのターン開始（TurnManager から呼ばれる）
void BattleScene::OnPlayerTurnStart()
{
    ResetTurnState();
    TurnBanner::Show(TurnBannerType::Player);
    UpdateTrapsAtTurnStart();   // 罠が育つ・導火線
    m_player->RestoreEnergy();
    m_player->AddEnergy(RelicManager::SumValue("turnEnergy"));   // レリック
    CarryOverBlock();
    m_player->GetBuffManager().TickDurations(false);   // プレイヤーのバフ：ターンの始めに減る（敵のターンの間も残る）
    ApplyTurnStartPowers();
    TickPlayerPoison();

    m_turnCount++;
    if ((m_turnCount % 4) == 0 && !m_bossAwakened) SwapWedges();   // 3層ボス：4ターン毎に楔が位置交換
    DecideEnemyActions();
    MarkBossAwakenIntent();
    m_arrowRevealTimer = 0.1f;   // 敵の動作直後に次矢印が出ないよう一拍おく

    DrawTurnHand();
}

// 敵のターン開始＝プレイヤーのターンの終わり（TurnManager から呼ばれる）
void BattleScene::OnEnemyTurnStart()
{
    TickTurnEndDurations();
    ApplyTurnEndPowers();
    TurnBanner::Show(TurnBannerType::Enemy);
    for (auto enemy : m_enemies)
        enemy->ResetBlock();
    float burnEnd = DiscardHandAtTurnEnd();

    m_enemyPhase = EnemyTurnPhase::WaitStart;
    m_currentEnemyIdx = 0;
    m_poisonIdx = 0;         
    m_poisonSubTicks = 0;
    m_enemyActionDelay = max(0.8f, burnEnd + 0.25f);   // 過負荷が全て終わるまで敵は動かない
}

// このターンの数を戻す（移動した数・廃棄した数・煤けた手袋・このターンだけのコスト・1ターン用のレリック）
void BattleScene::ResetTurnState()
{
    m_player->ResetMoved();
    m_burnedThisTurn = 0;              // このターンに燃やした枚数（煤けた火打石）
    m_burnStatusEnergyUsed = false;    // 煤けた手袋（1ターンに1回）
    for (auto* c : m_hand.GetCards()) c->SetCostThisTurn(-1);   // 「このターンだけのコスト」を戻す
    ResetPerTurnRelicCounters();
}

// ブロックの持ち越し：一番多く残る手段だけを使う（重ねて増えないように）
void BattleScene::CarryOverBlock()
{
    int block = m_player->GetBlock();
    int keep = block * min(100, RelicManager::SumValue("blockRetain")) / 100;   // 割合（鋼の意志）
    if (RelicManager::HasKind("blockCarry"))                                    // 一定量だけ減る（不落の城壁）
        keep = max(keep, block - RelicManager::SumValue("blockCarry"));
    if (m_player->GetBuffManager().HasBuff(BuffType::HalfCarry)) keep = max(keep, block / 2);   // 残心は半分
    if (m_player->GetBuffManager().HasBuff(BuffType::Barricade)) keep = block;  // 土手・要塞は全部
    m_player->SetBlock(keep);
}

// プレイヤーの毒：今の値だけダメージを受けてから、1減る（敵と同じ TickPoison。HPバーの予告の数字と同じになる）
void BattleScene::TickPlayerPoison()
{
    int poisonDmg = m_player->GetBuffManager().TickPoison();
    if (poisonDmg > 0)
        m_player->TakeDamage(poisonDmg, DamageFeel::Poison);
}

// プレイヤーのターンの終わり：残りターンを減らす（プレイヤーのデバフ・敵のバフ・時限床）
void BattleScene::TickTurnEndDurations()
{
    // 残りターン：プレイヤーのデバフはプレイヤーのターンの終わり、敵のバフは敵のターンの始めに減る
    m_player->GetBuffManager().TickDurations(true);
    for (auto enemy : m_enemies)
        if (enemy->GetHp() > 0) enemy->GetBuffManager().TickDurations(false);

    // 時限床（ボスのやけど床など）の寿命：敵ターン開始時に1減らし、0で消滅
    for (int r = 0; r < m_gridMap->GetRows(); r++)
        for (int c = 0; c < m_gridMap->GetCols(); c++)
        {
            auto& cell = m_gridMap->GetCell(c, r);
            if (cell.tileEffect.active && cell.tileEffect.hazardTurns > 0)
            {
                cell.tileEffect.hazardTurns--;
                if (cell.tileEffect.hazardTurns <= 0)
                    cell.tileEffect = TileEffect();
            }
        }
}
