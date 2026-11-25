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
// （BattleScene のメンバー。BattleScene.cpp から機能ごとに分けたファイル）

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
