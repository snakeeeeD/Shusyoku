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

// BattleScene は機能ごとにファイルを分けている（どれも同じ BattleScene クラスのメンバー）
//   BattleScene.cpp            初期化と毎フレームの更新
//   BattleSceneInput.cpp       入力
//   BattleSceneDraw.cpp        描画
//   BattleSceneDebug.cpp       デバッグ用の窓
//   BattleSceneEnemyFx.cpp     敵の攻撃の演出
//   BattleSceneEnemies.cpp     敵の出入り
//   BattleSceneResult.cpp      勝ち負け
//   BattleSceneHand.cpp        手札
//   BattleSceneBurn.cpp        廃棄の軸
//   BattleSceneRelic.cpp       レリックの呼び出し口
//   BattleSceneSequence.cpp    時間を置いて1つずつ進める処理
//   BattleSceneTrap.cpp        罠
//   BattleSceneBoss.cpp        ボスの仕掛け
//   BattleSceneCameraLink.cpp  シーン側のカメラ
//   BattleSceneFx.cpp          盤面の空気とバフの演出
//   BattleSceneQuery.cpp       調べるだけの関数
//   BattleSceneTurn.cpp        ターンの流れ
//   BattleScenePowers.cpp      ターンの節目に発動するバフ
//   BattleSceneEnemyTurn.cpp   敵のターン（段階ごと）
//   BattleSceneAim.cpp         狙い（範囲のハイライト・構え）
//   BattleSceneHint.cpp        その場ヒント

// ナイフ＋投擲術なら遠距離化（Diamond=範囲内の任意の敵を狙える。単体パスのままなので研磨も効く）
void BattleScene::ApplyKnifeThrow(CardData& d, const Player* p)
{
    if (p->GetBuffManager().HasBuff(BuffType::KnifeThrow)
        && std::find(d.tags.begin(), d.tags.end(), "Knife") != d.tags.end())
    {
        d.rangeType = RangeType::Diamond;
        d.range = p->GetBuffManager().GetBuffValue(BuffType::KnifeThrow);   // バフ値＝射程
    }
}

BattleScene::BattleScene()
    : m_battleUI(nullptr)
    , m_gridMap(nullptr)
    , m_whiteTexture(nullptr)
    , m_playerCol(0)
    , m_playerRow(0)
    , m_player(nullptr)
    , m_renderer3D(nullptr)
    , m_spriteRenderer(nullptr)
    , m_highlightTimer(0.0f)
{
}

BattleScene::~BattleScene()
{
    EnemyIntentVisual::SetPlayerBuffs(nullptr);
    CardExecutor::SetBattlePlayer(nullptr);
    EnemyIntentVisual::SetHidden(false);   // 目隠しを戦闘の外に持ち越さない
    delete m_battleUI;
    delete m_gridMap;
    delete m_renderer3D;
    delete m_spriteRenderer;
    delete m_player;
    for (auto enemy : m_enemies)
        delete enemy;
    m_enemies.clear();
}

bool BattleScene::Init(ID3D11Device* device, ID3D11DeviceContext* context, 
                       int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    m_selectedCardIndex = -1;
    m_hoveredCardIndex  = -1;

    m_showDrawPile = false;
    m_showDiscardPile = false;
    m_showExhaustPile = false;

    m_hoveredCell = { -1, -1 };

#ifdef _DEBUG
    m_debugRank = 1;
    m_debugEncounterIndex = -1;
#endif

    m_battleResult = BattleResult::None;

    InitDeckAndHand();                                   // 手札：デッキ・最初の手札
    if (!InitRenderers(device, context, screenWidth, screenHeight, swapChain)) return false;
    InitBoardAndPlayer();                                // 盤面・プレイヤーの位置とHP・カメラ
    ApplyStartRelics();                                  // レリック：戦闘開始時
    m_turnManager.onPlayerTurnStart = [this]() { OnPlayerTurnStart(); };   // ターン（BattleSceneTurn.cpp）
    m_turnManager.onEnemyTurnStart  = [this]() { OnEnemyTurnStart(); };

    Se::SetFloor(PlayerDataManager::GetData().layer);   // 足音をこの層の地面に
    SpawnEncounter();                                    // 敵の出入り：エンカウントの敵
    HitStop::Clear();   // 前の戦闘の停止を持ち越さない
    DecideEnemyActions();

    m_input.SetWindowHandle(hWnd);
    ClearBattleEffects();
    RunTurnCycle();
    TurnBanner::ShowThen(TurnBannerType::BattleStart, TurnBannerType::Player);
    PlaceDecos();
    LogBattleStart();
    return true;
}

// 描画まわり（3D・2D・戦闘UI）
bool BattleScene::InitRenderers(ID3D11Device* device, ID3D11DeviceContext* context, int screenWidth, int screenHeight, IDXGISwapChain* swapChain)
{
    m_renderer3D = new Renderer3D();
    if (!m_renderer3D->Init(device, context, screenWidth, screenHeight))
        return false;

    m_renderer3D->InitPostProcess();
    m_renderer3D->InitShadowMap();   // 落ち影。作れなければ前のにせ影のまま

    m_spriteRenderer = new SpriteRenderer();
    m_spriteRenderer->Init(device, context, screenWidth, screenHeight);

    m_battleUI = new BattleUI();
    if (!m_battleUI->Init(device, context, screenWidth, screenHeight, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");
    return true;
}

// 盤面と、プレイヤーの位置・HP・カメラ
void BattleScene::InitBoardAndPlayer()
{
    auto& playerData = PlayerDataManager::GetData();

    // グリッドマップ
    m_gridMap = new GridMap();
    m_gridMap->Init(9, 7, 72.0f);

    // 各マスに白いテクスチャをセット
    for (int row = 0; row < m_gridMap->GetRows(); row++)
        for (int col = 0; col < m_gridMap->GetCols(); col++)
            m_gridMap->GetCell(col, row).gameObject.texture = m_whiteTexture;

    // プレイヤー初期位置
    m_playerCol = m_gridMap->GetCols() / 2;
    m_playerRow = m_gridMap->GetRows() / 2;
    m_gridMap->SetCellType(m_playerCol, m_playerRow, CellType::Player);

    m_player = new Player();
    CardExecutor::SetBattlePlayer(m_player);   // 罠のダメージに罠術バフを足すため
    m_player->gridCol = m_playerCol;
    m_player->gridRow = m_playerRow;
    m_player->PlaceAt((m_playerCol - m_gridMap->GetCols() / 2.0f) * 1.1f,
        (m_playerRow - m_gridMap->GetRows() / 2.0f) * 1.1f);   // 正面を向いて立つ

    m_camera.Home(m_player->worldX, m_player->worldZ);

    // PlayerDataManagerからHPを引き継ぐ
    m_player->SetMaxHp(playerData.maxHp);   // 最大HPをPlayerDataから同期
    m_player->SetHp(playerData.hp);
    m_player->SnapDisplayHp();   // 表示HPを即実HPに（入場時のアニメ防止）
}


void BattleScene::Update(float deltaTime)
{
    m_input.Update();
    if (m_player) EnemyIntentVisual::SetPlayerBuffs(&m_player->GetBuffManager());   // 予告表示に被ダメ補正を反映
    UpdateIntentVisibility();                 // 目隠しが手札にある間は敵の予告を隠す
    SyncFaithBuffs();
    m_screenFx.Update(deltaTime, { m_player->GetHp(), m_player->GetMaxHp(),
        m_player->GetBuffManager().HasBuff(BuffType::Poison),
        m_player->GetBuffManager().HasBuff(BuffType::Burn) });   // 画面の演出（演出中・ヒットストップ中も止めない）
    SpawnAmbientParticles(deltaTime);         // 信仰の光の粒・背景の火の粉
    UpdateBossGimmick();   // 3層ボス：楔の生存で無敵/覚醒を制御
    if (UpdateAwakenCinematic(deltaTime)) return;   // 覚醒演出の間は演出だけ動かす
    if (m_freeLook) return;

#ifdef _DEBUG
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
#endif

    if (HitStop::IsActive()) { HitStop::Update(deltaTime); return; }   // ヒットストップ中はゲームを止める

    FloatingTextManager::Update(deltaTime);
    BuffPopupManager::Update(deltaTime);
    UpdateSequences(deltaTime);               // 順番待ち（連撃の追撃・敵の多段・罠の連鎖など）
    UpdatePresentation(deltaTime);            // お知らせ・揺れ・バナー・エフェクト・引き寄せの線・床の演出
    UpdateFuseBeep();                         // 導火線のピッ
    UpdateBuffGainFx();   // バフが付いた／増えたらエフェクト
    UpdateSnakeMolt();                        // オロチの脱皮
    UpdateDyingEnemies(deltaTime);            // HP0の敵の塵化と片付け
    UpdateUnits(deltaTime);                   // プレイヤーと敵の動き・HP表示

    HandleRightClickCancel();                 // 右クリックで経路→カード選択の順に取り消す
    HandleCameraInput();                      // ホイール・右ドラッグ（単押しで選択解除）
    UpdateAimCamera(deltaTime);               // 狙いカメラ・操作デモ・カメラ更新
    m_highlighter.SetSelectedEnemy(FocusEnemy());
    UpdateCardHighlight(CardVisual::CARD_HIDE_Y_OFFSET);   // 選んでいるカードの範囲（※最後にもう一度、手札の上端を変えて更新している）
    m_highlightTimer += deltaTime * 0.5f; // 点滅速度調整（0に戻さない：戻すと整数倍でない点滅・色の入れ替わりがワープする）
    if (m_arrowRevealTimer > 0.0f) m_arrowRevealTimer -= deltaTime;
    if (m_battleResult != BattleResult::None) { UpdateAfterBattleEnd(deltaTime); return; }

    UpdateEnemyHighlight();                   // 敵の攻撃範囲
    UpdateHints();                            // その場ヒント
    UpdateDrawSequence(deltaTime);            // 山札を戻す演出・時間差のドロー
    PileFx::Update(deltaTime);   // 山札・捨て札の光の帯と弾み
    m_battleUI->UpdatePlayCardEffects(deltaTime);
    UpdateOverloadHits(deltaTime);            // 過負荷のダメージ
    UpdateReadyPose();                        // 攻撃カードを選んでいる間は構える
    UpdateHandAnimations(deltaTime);          // 手札のアニメ
    if (CheckBattleEnd()) return;             // 勝ち／不死鳥の尾羽／負け
    if (m_turnManager.IsEnemyTurn()) UpdateEnemyTurn(deltaTime);   // 敵のターン（BattleSceneEnemyTurn.cpp）
    UpdateCardHighlight(CardVisual::CARD_H + CardVisual::CARD_HOVER_Y_OFFSET);
}

// プレイヤーと敵の動き・HP表示・当たった時の光
void BattleScene::UpdateUnits(float deltaTime)
{
    m_player->UpdateDisplayHp(deltaTime);
    for (auto enemy : m_enemies)
        enemy->UpdateDisplayHp(deltaTime);

    m_player->UpdateMove(deltaTime);
    m_player->Update(deltaTime);
    m_player->UpdateHitFlash(deltaTime);

    for (auto enemy : m_enemies)
    {
        enemy->UpdateMove(deltaTime);
        enemy->UpdateLunge(deltaTime);
        enemy->UpdateJump(deltaTime);
        enemy->UpdateHitFlash(deltaTime);
        enemy->FaceToward(m_player->worldX);
    }
}

