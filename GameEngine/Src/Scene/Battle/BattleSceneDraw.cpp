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

// BattleScene の描画（Draw とその下の関数）。状態はシーンのものをそのまま読むので、クラスは分けずにファイルだけ分ける
// 中身は BattleSceneDrawBoard（盤面）/ DrawMarks（地面の印）/ DrawUnits（キャラ）/ DrawOverlay（上に重ねる）.cpp

// 描く順番を並べるだけ（中身は下の関数）。奥・地面から順に重ねる
void BattleScene::Draw()
{
    RequestCellCursor();                 // 使えるマス・手札の上なら指差し
    bool blurScene = (m_awakenCinematic > 0.0f);
    if (blurScene) m_renderer3D->BeginOffscreen();   // 覚醒演出：ぼかすためにいったん別の絵に描く
    DrawBackground2D();

    m_renderer3D->Begin();
    UpdateCellHeights();                 // 狙うマスを浮かせる＋その上に立つキャラの高さ（描く前に）
    const BattleTheme th = CurrentTheme();
    DrawShadowPass(th);                  // 落ち影：光から見た深さの絵（地面より先に）
    DrawGround(th);                      // 地面のタイル・装飾と影
    DrawCells(th);                       // マス
    DrawFloorMarks();                    // 信仰・敵の範囲と外周線・自分の範囲
    DrawTraps();
    DrawGroundEffects();                 // 地面の順番のエフェクト（衝撃波の輪、壁の奥半分など）。キャラより先に描いて、キャラの後ろに見せる
    DrawSnakes();
    DrawUnits();                         // 身代わり・×印・注目の枠・背水のオーラ・脅威のグロー・キャラ・聖地の光
    DrawOverlays();                      // エフェクト・必中のクロスヘア・移動先のゴースト・L字枠
    m_renderer3D->End();

    if (blurScene) DrawAwakenCinematic();   // 集中線・ブラー・ボスだけくっきり・円
    m_renderer3D->Begin();                                   // 画面の順番のエフェクト（瘴気など）。3Dの上・UIの下
    EffectManager::Draw(m_renderer3D, EffectLayer::Screen);
    m_renderer3D->End();
    m_screenFx.Draw(m_renderer3D, m_spriteRenderer, (float)m_screenWidth, (float)m_screenHeight);   // 画面の演出（3Dの上・UIの下）
    BattleUIContext ctx = MakeUIContext();
    m_battleUI->Draw(ctx);
    m_battleUI->DrawAddedCardsTop(ctx);
}

// UI・帯より上の演出（まるのみの顎）。その上に GAME OVER（顎で画面が覆われても見えるように）
void BattleScene::DrawTopEffects()
{
    m_renderer3D->Begin();
    EffectManager::Draw(m_renderer3D, EffectLayer::Top);
    m_renderer3D->End();
    if (m_battleResult == BattleResult::Lose) m_battleUI->DrawLoseBanner();
}

// マウスカーソル：手札のカード／今離したら使えるマスの上なら指差し（ボタン類は UiHover・HoverLift 側で出る）
void BattleScene::RequestCellCursor()
{
    if (m_battleResult == BattleResult::None && m_turnManager.IsPlayerTurn())
    {
        bool hov = false;
        int sel = m_selectedCardIndex;
        if (sel >= 0 && sel < (int)m_hand.GetCards().size())
        {
            CardData d = *m_hand.GetCards()[sel]->GetData();
            ApplyKnifeThrow(d, m_player);
            if (d.type == CardType::Move)
                hov = !m_movePath.empty() && m_movePath.back() == m_hoveredCell;   // 経路の先＝離せばそこへ行ける
            else if (SelectedNeedsTarget())
                hov = m_hoveredCell.first >= 0 && CellInCardRange(d, m_hoveredCell.first, m_hoveredCell.second);
        }
        else hov = (m_hoveredCardIndex >= 0);   // 何も持っていない時は手札のカード
        if (hov) Cursor::Request(CursorKind::Hover);
    }
}

// 2Dの背景（3Dの地面の後ろ）
void BattleScene::DrawBackground2D()
{
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(
        TextureManager::Get("battle_bg"),
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(0.8f, 0.8f, 0.8f, 1.0f));
    m_spriteRenderer->End();
}

// 背景テーマ（層＋ボスで地面・装飾・マスの色を決める）。層やボスを足す時はここに1つ
BattleScene::BattleTheme BattleScene::CurrentTheme() const
{
    int themeLayer = PlayerDataManager::GetData().layer;
    BattleTheme th;
    if (m_category == EncCategory::Boss && m_battleEnemyId == "l3boss")
    {
        if (m_bossAwakened)
            th = { "ground_void_awake", { "deco_rift","deco_debris","deco_throne" }, { 1.4f,1.9f,2.1f }, { 3.2f,2.2f,4.2f },
                   "cell_stone", 4, { 0.60f, 0.42f, 0.52f, 0.92f } };
        else
            th = { "ground_void",       { "deco_rift","deco_debris","deco_throne" }, { 1.4f,1.9f,2.1f }, { 3.2f,2.2f,4.2f },
                   "cell_stone", 4, { 0.52f, 0.48f, 0.62f, 0.92f } };
    }
    else if (m_category == EncCategory::Boss && m_battleEnemyId == "l2boss")
        th = { "ground_snakepit", { "deco_darkspike","deco_bones","deco_egg" }, { 2.2f,4.3f,3.4f }, { 3.8f,2.1f,2.1f },
               "cell_stone", 4, { 0.50f, 0.56f, 0.44f, 0.92f } };
    else if (m_category == EncCategory::Boss)
        th = { "ground_scorched", { "deco_deadtree","deco_ember","deco_darkrock" }, { 2.8f,3.3f,3.1f }, { 3.8f,2.1f,2.1f },   // dragon(1層)
               "cell_stone", 4, { 0.55f, 0.47f, 0.43f, 0.92f }, 1.0f, "ambient_ember", 10.0f };   // 火の粉が舞う
    else if (themeLayer <= 1)
        th = { "ground_grass",    { "deco_tree","deco_bush","deco_rock" },          { 3.2f,2.5f,3.3f }, { 3.8f,1.8f,2.1f },
               "cell_stone", 4, { 0.80f, 0.76f, 0.66f, 0.92f } };
    else if (themeLayer == 2)
        th = { "ground_cave",     { "deco_stalagmite","deco_crystal","deco_caverock" }, { 1.9f,2.4f,3.1f }, { 3.8f,3.4f,2.1f },
               "cell_stone", 4, { 0.60f, 0.66f, 0.78f, 0.92f }, 0.85f };   // 洞窟は少し暗く
    else
        th = { "ground_future",   { "deco_pillar","deco_terminal","deco_techblock" }, { 1.2f,2.6f,3.2f }, { 3.8f,3.0f,2.2f },
               "cell_panel", 1, { 0.62f, 0.70f, 0.78f, 0.92f }, 0.85f };   // 近未来も少し暗く
    return th;
}

// 戦闘UIに渡す情報
BattleUIContext BattleScene::MakeUIContext()
{
    BattleUIContext ctx;
    ctx.player = m_player;
    ctx.enemies = &m_enemies;
    ctx.hand = &m_hand;
    ctx.deck = &m_deck;
    ctx.turnManager = &m_turnManager;
    ctx.renderer3D = m_renderer3D;
    ctx.gridMap = m_gridMap;
    ctx.playerCol = m_playerCol;
    ctx.playerRow = m_playerRow;
    ctx.selectedCardIndex = m_selectedCardIndex;
    ctx.hoveredCardIndex = m_hoveredCardIndex;
    ctx.hoveredCell = m_uiBlocked ? std::pair<int, int>(-1, -1) : m_hoveredCell;   // 窓の後ろでマスの説明を出さない
    ctx.highlightTimer = m_highlightTimer;
    ctx.battleResult = m_battleResult;
    ctx.drops = &m_dropResult;
    ctx.showDrawPile = m_showDrawPile;
    ctx.cardSelecting = m_cardSelecting;
    ctx.showDiscardPile = m_showDiscardPile;
    ctx.showExhaustPile = m_showExhaustPile;
    ctx.pileHideIndex = m_reclaimHideIdx;
    ctx.screenWidth = m_screenWidth;
    ctx.screenHeight = m_screenHeight;
    ctx.cameraZoom = m_camera.Zoom();
    ctx.isPlayerTurn = m_turnManager.IsPlayerTurn();
    ctx.showMoveArrows = (!m_turnManager.IsPlayerTurn() || m_arrowRevealTimer <= 0.0f)
        && !EnemyIntentVisual::Hidden();   // 目隠し中は移動予定も見せない
    ctx.mousePos = m_uiBlocked ? POINT{ -10000, -10000 } : m_input.GetMousePos();   // 窓の後ろのボタンにホバーさせない
    ctx.outOfRangeCells = &m_highlighter.GetOutOfRangeCells();
    ctx.travelPath = &m_movePath;
    ctx.selectedEnemy = FocusEnemy();
    ctx.discardSelectCount = m_discardSelectCount;
    ctx.discardSelected = &m_discardSelected;
    ctx.discardViewMode = m_discardViewMode;
    ctx.burnSelect = m_selectBurn;
    ctx.rewardRelic = &m_rewardRelic;
    return ctx;
}
