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
#include "ScreenShake.h"
#include "HitStop.h"
#include "TurnBanner.h"
#include "DamageFeedback.h"
#include "RangeShape.h"
#include "UiNotice.h"
#include "Cursor.h"
#include "Audio.h"
#include "Hint.h"
#include "PileFx.h"
#include "Telemetry.h"
#include "EnemyIntentVisual.h"
#include "Faith.h"

#include <algorithm>
#include <cstdio>
#include <queue>
#include <map>
#include <set>

#ifdef _DEBUG
#include "External/imgui/imgui.h"
#endif

// 選ぶ画面の入力（捨てる／廃棄するカード・山札などの一覧から選ぶ）
// （BattleScene のメンバー。BattleSceneInput.cpp から機能ごとに分けたファイル）

// 捨てる／廃棄するカードを選ぶ画面
void BattleScene::HandleDiscardSelectInput()
{
    m_highlighter.ClearPlayerHighlight(m_gridMap);      // グリッドの範囲表示を出さない

    POINT mp = m_input.GetMousePos();
    bool click = m_input.GetMouseButtonTrigger(0);

    if (click && m_battleUI->IsOnDiscardView(mp))       // 閲覧モード切替
    {
        m_discardViewMode = !m_discardViewMode;
        return;
    }

    if (m_discardViewMode) { HandleDiscardViewMode(mp, click); return; }

    // 「捨てる」「盤面を見る」の上では説明を消す（説明の窓がボタンに重なって押せなくなるため）
    bool onBtn = m_battleUI->IsOnDiscardConfirm(mp) || m_battleUI->IsOnDiscardView(mp);
    if (onBtn) CardTooltip::ClearHeld();
    m_hoveredCardIndex = onBtn ? -1 : m_battleUI->GetCardAtScreenPos(mp);
    if (m_hoveredCardIndex < 0 && !onBtn && CardTooltip::MouseInHold())   // 説明へ移る間はカードを浮かせたまま
        m_hoveredCardIndex = m_prevHoveredCard;

    if (m_hoveredCardIndex != m_prevHoveredCard)
    {
        if (m_hoveredCardIndex >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_prevHoveredCard = m_hoveredCardIndex;
    }

    if (click)
    {
        if (m_battleUI->IsOnDiscardConfirm(mp)
            && (int)m_discardSelected.size() == m_discardSelectCount) { ConfirmDiscardSelect(); return; }   // 確定
        ToggleDiscardSelect(m_hoveredCardIndex);
    }
}

// 「盤面を見る」の間：山札などのボタンで一覧を開く／一覧の外で閉じる（手札は選ばない）
void BattleScene::HandleDiscardViewMode(POINT mp, bool click)
{
    if (click)
    {
        const float pileSW = (float)m_screenWidth, pileSH = (float)m_screenHeight;
        bool onDraw = PileFx::OnPile(PileFx::Pile::Draw, pileSW, pileSH, (float)mp.x, (float)mp.y);
        bool onDiscard = PileFx::OnPile(PileFx::Pile::Discard, pileSW, pileSH, (float)mp.x, (float)mp.y);
        bool onExhaust = PileFx::OnPile(PileFx::Pile::Exhaust, pileSW, pileSH, (float)mp.x, (float)mp.y);
        bool onPileBtn = onDraw || onDiscard || onExhaust;

        if (onPileBtn)
        {
            if (onDraw)    m_showDrawPile = !m_showDrawPile;
            if (onDiscard) m_showDiscardPile = !m_showDiscardPile;
            if (onExhaust) m_showExhaustPile = !m_showExhaustPile;
        }
        else if (m_showDrawPile || m_showDiscardPile || m_showExhaustPile)
        {
            // ビューアの外をクリックで閉じる
            float bgX = m_screenWidth / 2.0f - 300.0f, bgY = 50.0f;
            if (mp.x < bgX || mp.x > bgX + 600.0f || mp.y < bgY || mp.y > bgY + 580.0f)
            {
                m_showDrawPile = false;
                m_showDiscardPile = false;
                m_showExhaustPile = false;
            }
        }
    }
    m_hoveredCardIndex = -1;
}

// 選んだカードを確定：廃棄の画面なら廃棄して効果、捨てる画面なら捨てて「捨てた時」の効果（古い手帳のブロックも）
void BattleScene::ConfirmDiscardSelect()
{
    if (m_selectBurn)   // 燃やす画面：捨てる代わりに燃やす
    {
        std::vector<int> sel = m_discardSelected;
        CancelDiscardSelect();
        ApplyBurnEffect(m_burnSource, BurnHandCards(sel), m_burnTarget);
        m_hoveredCardIndex = -1;
        return;
    }
    std::sort(m_discardSelected.rbegin(), m_discardSelected.rend());
    std::vector<CardEffectData> effects;
    float playDelay = 0.0f;
    for (int idx : m_discardSelected)
    {
        const CardData* dd = CardDataBase::Get(m_hand.GetCards()[idx]->GetId());
        if (dd && dd->onDiscardEffect.hasEffect)
        {
            effects.push_back(dd->onDiscardEffect);
            m_battleUI->StartPlayCardEffectFromHand(dd, idx, playDelay, false); // 中央へ移動＝使用演出
            playDelay += 0.15f;                                                 // 複数枚は少しずらす
        }
        else
        {
            m_battleUI->StartDiscardEffectAt(idx);   // 効果なしは普通の捨てアニメ
        }
        m_hand.DiscardAt(idx);
        m_battleUI->OnCardRemoved(idx);
    }
    // 古い手帳：選んで捨てた枚数ぶんブロック
    if (int per = RelicManager::SumValue("discardBlock"); per > 0 && !m_discardSelected.empty())
    {
        m_player->AddBlock(per * (int)m_discardSelected.size());
        for (auto* d : RelicManager::OwnedByKind("discardBlock")) RelicManager::NotifyFired(d->id);   // 帯のアイコンを光らせる
    }
    m_discardSelected.clear();
    m_discardSelectCount = 0;
    m_hoveredCardIndex = -1;
    for (auto& e : effects) ApplyDiscardEffect(e);   // 捨て終わってから発動
    return;
}

// 手札クリックで選ぶ／外す（上限なら一番古いのを外す）
void BattleScene::ToggleDiscardSelect(int idx)
{
    if (idx >= 0 && idx < (int)m_hand.GetCards().size())
    {
        auto it = std::find(m_discardSelected.begin(), m_discardSelected.end(), idx);
        if (it != m_discardSelected.end())
        {
            m_discardSelected.erase(it);                  // 選択済み → 解除
        }
        else
        {
            if ((int)m_discardSelected.size() >= m_discardSelectCount)
                m_discardSelected.erase(m_discardSelected.begin());   // 上限なら一番古いのを外す
            m_discardSelected.push_back(idx);
        }
    }
}

// 一覧から選ぶ（山札から探す・捨て札から回収・廃棄札から戻す）。選んだら／枠の外なら閉じる
void BattleScene::HandlePilePick(POINT mousePos)
{
    auto displayPile = BattleUI::PileForView(&m_deck, m_showDrawPile, m_showDiscardPile, m_reclaimHideIdx);
    int i = m_battleUI->PileCardAt(mousePos, (int)displayPile.size());   // 描画と同じ矩形で判定
    if (i >= 0)
    {
        std::string selectedId = displayPile[i];
        std::string result;
        if (m_selectingType == CardEffectType::Search)
            result = m_deck.DrawSpecificCard(selectedId);
        else if (m_selectingType == CardEffectType::Salvage)
            result = m_deck.SalvageCard(selectedId);
        else if (m_selectingType == CardEffectType::Reclaim)
            result = m_deck.ReclaimCard(selectedId);   // 同じカードが2枚でも前から取る（一番後ろの使ったばかりの1枚は取らない）

        if (!result.empty())
        {
            if (m_selectingType == CardEffectType::Reclaim) AddToHand(result, 0);   // 灰からの再生：このターンコスト0
            else
            {
                m_hand.AddCard(result);
                m_battleUI->StartDrawCardEffect(result);
            }
        }
    }
    if (i >= 0 || !m_battleUI->PileViewerRect().has(mousePos))   // 選んだ／枠の外＝取りやめ
    {
        m_cardSelecting = false;
        m_selectingType = CardEffectType::None;
        m_showDrawPile = false;
        m_showDiscardPile = false;
        m_showExhaustPile = false;
        m_reclaimHideIdx = -1;
    }
}
