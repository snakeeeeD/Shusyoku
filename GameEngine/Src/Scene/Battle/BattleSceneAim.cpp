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

// 狙い（選んでいるカードの範囲・敵の攻撃範囲のハイライト・攻撃の構え）

// 選んでいる（なければ指している）カードの範囲のハイライト。handTopOffset＝手札の範囲の上端（画面の下からの距離）
void BattleScene::UpdateCardHighlight(float handTopOffset)
{
    int highlightCardIndex = m_selectedCardIndex >= 0 ? m_selectedCardIndex : m_hoveredCardIndex;
    if (highlightCardIndex >= 0 && highlightCardIndex < (int)m_hand.GetCards().size())
    {
        CardData dataThrow = *m_hand.GetCards()[highlightCardIndex]->GetData();
        ApplyKnifeThrow(dataThrow, m_player);
        const CardData* data = &dataThrow;

        RECT cardArea = { 0, 0, 0, 0 };
        if (!m_hand.GetCards().empty())
        {
            int numCards = (int)m_hand.GetCards().size();
            float totalW = numCards * (CardVisual::CARD_W + 10.0f);
            float leftX = m_screenWidth / 2.0f - totalW / 2.0f;
            float rightX = leftX + totalW;
            float topY = m_screenHeight - handTopOffset;
            cardArea = { (LONG)leftX, (LONG)topY, (LONG)rightX, (LONG)m_screenHeight };
        }

        m_highlighter.SetTravelPath(&m_movePath);

        m_highlighter.UpdatePlayerHighlight(
            m_playerCol, m_playerRow, data,
            m_enemies, m_gridMap, m_player,
            m_highlightTimer, m_hoveredCell,
            m_renderer3D, m_screenWidth, m_screenHeight,
            cardArea, m_player->GetBuffManager().HasBuff(BuffType::MoveLock), AttackTarget());
    }
    else
    {
        m_highlighter.ClearPlayerHighlight(m_gridMap);
    }
}

// 敵の攻撃範囲のハイライト
void BattleScene::UpdateEnemyHighlight()
{
    m_highlighter.UpdateEnemyHighlight(
        m_enemies, m_gridMap, m_player,
        m_playerCol, m_playerRow, m_highlightTimer,
        m_decoyCol, m_decoyRow);
}

// 攻撃カードを選んでいる間は、剣を振りかぶって構える（狙っている先の方を向く）
void BattleScene::UpdateReadyPose()
{
    bool ready = m_selectedCardIndex >= 0 && m_selectedCardIndex < (int)m_hand.GetCards().size()
        && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type == CardType::Attack;
    m_player->SetReady(ready);
    if (ready)   // 狙っている先（今離したら当たる敵／射程内でカーソルを乗せているマス）の方を向く
    {
        const CardData* d = m_hand.GetCards()[m_selectedCardIndex]->GetData();
        int tc = -1, tr = -1;
        if (const Enemy* t = AttackTarget())
        {
            int best = 9999;   // 大きい敵は、自分に一番近いマス
            for (auto& [c, r] : t->OccupiedCells())
            {
                int dd = abs(c - m_playerCol) + abs(r - m_playerRow);
                if (dd < best) { best = dd; tc = c; tr = r; }
            }
        }
        else if (m_hoveredCell.first >= 0 && CellInCardRange(*d, m_hoveredCell.first, m_hoveredCell.second))
        {
            tc = m_hoveredCell.first; tr = m_hoveredCell.second;
        }
        if (tc >= 0) m_player->FaceToward(tc - m_playerCol, tr - m_playerRow);
    }
}
