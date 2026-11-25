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

// BattleScene の入力（HandleInput とその下の関数）。状態はシーンのものをそのまま使うので、クラスは分けずにファイルだけ分ける
// ほかは BattleSceneInputSelect（選ぶ画面）/ InputMove（移動）/ InputPlay（カードを使う）.cpp

// 戦闘の入力。どの操作をどこで受けるかの振り分けだけ（中身は下の関数）
void BattleScene::HandleInput()
{
    if (m_camera.DemoActive())   // カメラのデモ中は操作を止める（左クリックで飛ばす）
    {
        if (m_input.GetMouseButtonTrigger(0)) m_camera.EndDemo();
        return;
    }
    if (HandleBattleEndInput()) return;                                 // 勝敗後：クリックで次へ
    if (!m_turnManager.IsPlayerTurn()) return;                          // プレイヤーターン以外は無視
    if (m_drawSeqRemaining > 0 || m_reshuffleFxTimer > 0.0f) return;    // 時間差ドロー中／リシャッフル演出中は操作不可
    if (m_discardSelectCount > 0) { HandleDiscardSelectInput(); return; }   // 捨てるカードの選択中

    PickEnemyOnGrid();          // 盤面の敵クリックで範囲を見る
    UpdateHoveredCell();        // 指しているマス・狙い確定のタイマー
    UpdateMovePath();           // 移動カードの経路づくり
    HandleReleaseToPlay();      // 左ボタンを離したら使う
    HandleClickInput();         // 山札・捨て札ボタン、一覧から選ぶ、クリックで使う
    HandleHandInput();          // 手札を選ぶ・ホバー
    HandleTurnEndAndPanel();    // ターン終了ボタン、敵パネル
}

// 勝敗後：クリックで次へ（層を進める／報酬／結果画面）。勝敗が付いていたら true
bool BattleScene::HandleBattleEndInput()
{
    // 勝敗後はターンに関係なくクリックで進める
    if (m_battleResult == BattleResult::Win)
    {
        if (m_input.GetMouseButtonTrigger(0) && onChangeScene)
        {
            if (m_category == EncCategory::Boss)
            {
                auto& pd = PlayerDataManager::GetData();
                if (pd.layer < 3)
                {
                    pd.layer++;                       // 次の層へ
                    pd.fieldNodeTypes.clear();        // マップを作り直させる
                    pd.fieldNodeEnemyIds.clear();
                    pd.fieldNodeVisited.clear();
                    PlayerDataManager::Save();
                    onChangeScene(SceneType::Field);
                }
                else
                {
                    PlayerDataManager::DeleteSave();   // クリア＝セーブ削除
                    onChangeScene(SceneType::Result);
                }
            }
            else onChangeScene(SceneType::CardSelect);
        }
        return true;
    }
    if (m_battleResult == BattleResult::Lose)
    {
        if (m_input.GetMouseButtonTrigger(0) && onChangeScene)
        {
            PlayerDataManager::DeleteSave();   // ゲームオーバー＝セーブ削除
            onChangeScene(SceneType::Result);
        }
        return true;
    }
    return false;
}

// 盤面の敵をクリック → その敵の範囲を見る（もう一度で解除）
void BattleScene::PickEnemyOnGrid()
{
    if (m_input.GetMouseButtonTrigger(0) && m_selectedCardIndex < 0
        && m_input.GetMousePos().x < m_screenWidth - 250.0f      // パネル上は無視
        && m_input.GetMousePos().y < m_screenHeight - 150.0f)     // 手札エリアも無視
    {
        auto rc = m_gridMap->GetClickedCell3D(m_input.GetMousePos(),
            m_renderer3D->GetViewMatrix(), m_renderer3D->GetProjectionMatrix(),
            m_screenWidth, m_screenHeight);
        int found = -1;
        if (rc.cell)
            for (int i = 0; i < (int)m_enemies.size(); i++)
                for (auto& [dc, dr] : m_enemies[i]->GetGridShape())
                    if (m_enemies[i]->gridCol + dc == rc.col && m_enemies[i]->gridRow + dr == rc.row)
                        found = i;
        m_selectedEnemyRange = (found >= 0 && found != m_selectedEnemyRange) ? found : -1;
    }
}

// 指しているマス（m_hoveredCell）と、同じマスをいつから指しているか（狙い確定用）
void BattleScene::UpdateHoveredCell()
{
    // カード選択中はマウスのマスにハイライト
    if (m_selectedCardIndex >= 0)
    {
        POINT mousePos = m_input.GetMousePos();
        POINT gridMousePos = mousePos;
        if (gridMousePos.y > m_screenHeight + 200)
            gridMousePos.y = m_screenHeight + 200;
        auto result = m_gridMap->GetClickedCell3D(
            gridMousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
        else
            m_hoveredCell = { -1, -1 };   // マス外に出たら自動ホバー（敵）に戻す
    }
    else
    {
        POINT mousePos = m_input.GetMousePos();
        auto result = m_gridMap->GetClickedCell3D(
            mousePos,
            m_renderer3D->GetViewMatrix(),
            m_renderer3D->GetProjectionMatrix(),
            m_screenWidth,
            m_screenHeight
        );

        if (result.cell)
            m_hoveredCell = { result.col, result.row };
        else
            m_hoveredCell = { -1, -1 };

        m_highlighter.ClearPlayerHighlight(m_gridMap);
    }

    // 狙っているマスがいつから同じか
    if (m_hoveredCell != m_aimCell)
    {
        m_aimCell = m_hoveredCell;
        m_aimSince = GetTickCount64();
    }
}

// クリック：山札・捨て札・廃棄札ボタン、一覧から選ぶ（探す・回収）、盤面クリックで使う／外して解除
void BattleScene::HandleClickInput()
{
    if (!m_input.GetMouseButtonTrigger(0)) return;

    // 山札・捨て札ボタンの位置（PileFx：描く所と同じ）
    const float pileSW = (float)m_screenWidth, pileSH = (float)m_screenHeight;

    POINT mousePos = m_input.GetMousePos();

    // カードエリアにマウスがあるかチェック
    bool isOnCard = (m_battleUI->GetCardAtScreenPos(mousePos) >= 0);

    POINT gridMousePos2 = mousePos;
    if (gridMousePos2.y > m_screenHeight - 200)
        gridMousePos2.y = m_screenHeight - 200;
    auto result = m_gridMap->GetClickedCell3D(
        gridMousePos2,
        m_renderer3D->GetViewMatrix(),
        m_renderer3D->GetProjectionMatrix(),
        m_screenWidth,
        m_screenHeight
    );

    // 山札ボタン
    if (PileFx::OnPile(PileFx::Pile::Draw, pileSW, pileSH, (float)mousePos.x, (float)mousePos.y))
    {
        m_showDrawPile = !m_showDrawPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDiscardPile = false;
    }

    // 捨て札ボタン
    else if (PileFx::OnPile(PileFx::Pile::Discard, pileSW, pileSH, (float)mousePos.x, (float)mousePos.y))
    {
        m_showDiscardPile = !m_showDiscardPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDrawPile = false;
    }

    // 廃棄札ボタン
    else if (m_deck.GetExhaustPileCount() > 0
        && PileFx::OnPile(PileFx::Pile::Exhaust, pileSW, pileSH, (float)mousePos.x, (float)mousePos.y))
    {
        m_showExhaustPile = !m_showExhaustPile;
        Audio::PlaySE("Assets/Sound/se/click.mp3");
        m_showDrawPile = false;
        m_showDiscardPile = false;
    }

    // 一覧から選ぶ（山札から探す・捨て札から回収・廃棄札から戻す）
    else if (m_cardSelecting && (m_showDrawPile || m_showDiscardPile || m_showExhaustPile))
        HandlePilePick(mousePos);
    // 見るだけの一覧：枠の外で閉じる
    else if (m_showDrawPile || m_showDiscardPile || m_showExhaustPile)
    {
        if (!m_battleUI->PileViewerRect().has(mousePos))
        {
            m_showDrawPile = false;
            m_showDiscardPile = false;
            m_showExhaustPile = false;
        }
    }

    if (result.cell && !isOnCard)
    {
        if (m_selectedCardIndex >= 0
            && m_selectedCardIndex < (int)m_hand.GetCards().size()
            && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
            PlayCard(result.col, result.row, nullptr);   // クリックで使う
    }
    else if (m_selectedCardIndex >= 0 && !isOnCard
        && m_hand.GetCards()[m_selectedCardIndex]->GetData()->type != CardType::Move)
        m_selectedCardIndex = -1;
}

// 手札：クリックで選ぶ／同じカードで解除（エナジー不足は知らせて解除）、ホバー
void BattleScene::HandleHandInput()
{
    POINT mousePos = m_input.GetMousePos();
    int cardUnder = m_battleUI->GetCardAtScreenPos(mousePos);
    if (cardUnder >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        if (m_selectedCardIndex == cardUnder)
        {
            m_selectedCardIndex = -1;                       // 同じカード再クリック→解除
        }
        else
        {
            int cost = m_player->CostOf(*m_hand.GetCards()[cardUnder]->GetData(), m_hand.GetCards()[cardUnder]->CostThisTurn());
            if (m_player->GetEnergy() < cost)
            {
                // エナジー不足：アウトライン付き浮遊テキストで通知し、自動で選択解除
                float px = (m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f;
                float pz = (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f;
                FloatingTextManager::Spawn(px, 1.3f, pz, L"エナジーがたりない",
                    XMFLOAT4(1.0f, 0.45f, 0.2f, 1.0f), 30.0f);
                m_selectedCardIndex = -1;
            }
            else
            {
                m_selectedCardIndex = cardUnder;
            }
        }
    }

    m_hoveredCardIndex = m_battleUI->GetCardAtScreenPos(mousePos);
    if (m_selectedCardIndex >= 0) CardTooltip::ClearHeld();   // 選んでいる間は、残っている説明も消す（窓の上のクリックを盤面に通す）
    if (m_hoveredCardIndex != m_prevHoveredCard)
    {
        if (m_hoveredCardIndex >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
        m_prevHoveredCard = m_hoveredCardIndex;
    }
}

// ターン終了ボタン、敵パネルのクリック（範囲を見る＋その敵へカメラ）
void BattleScene::HandleTurnEndAndPanel()
{
    // ターンエンドボタン（右下）
    if (m_battleResult == BattleResult::None)
    {
        XMFLOAT4 er = PileFx::EndTurnRect((float)m_screenWidth, (float)m_screenHeight);   // 描く所と同じ
        float btnW = er.z, btnH = er.w, btnX = er.x, btnY = er.y;

        if (m_input.GetMouseButtonTrigger(0))
        {
            POINT mp = m_input.GetMousePos();
            if (mp.x >= btnX && mp.x <= btnX + btnW
                && mp.y >= btnY && mp.y <= btnY + btnH)
            {
                Audio::PlaySE("Assets/Sound/se/click.mp3");
                m_selectedCardIndex = -1;   // 選択解除してからターン終了
                m_cardSelecting = false;    // ドロー/捨札選択も解除
                m_turnManager.EndTurn();
            }
        }
    }

    // 敵パネルクリック → 範囲選択＋その敵へカメラ
    if (m_input.GetMouseButtonTrigger(0))
    {
        int i = BattleUI::EnemyEntryAt(m_input.GetMousePos(), m_enemies, m_screenWidth);
        if (i >= 0)
        {
            m_selectedEnemyRange = (m_selectedEnemyRange == i) ? -1 : i;
            m_camera.LookAt(m_enemies[i]->worldX, m_enemies[i]->worldZ);
        }
    }
}

// 右クリック（ボタン1）：経路キャンセル → カード選択キャンセル の二段階
void BattleScene::HandleRightClickCancel()
{
    if (m_input.GetMouseButtonTrigger(1)
        && m_selectedCardIndex >= 0
        && m_selectedCardIndex < (int)m_hand.GetCards().size())
    {
        bool isMove = (m_hand.GetCards()[m_selectedCardIndex]->GetData()->type == CardType::Move);

        if (isMove && !m_movePath.empty())
        {
            // 経路がある → 経路だけキャンセル（カードは維持）
            m_movePath.clear();
            m_moveReleaseSuppress = true;
            m_suppressCell = m_hoveredCell;
        }
        else
        {
            // 経路が無い or 移動以外のカード → カード選択解除
            m_selectedCardIndex = -1;
            m_movePath.clear();
            m_moveReleaseSuppress = false;
        }
    }
}
