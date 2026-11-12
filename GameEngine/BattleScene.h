#pragma once
#include "Scene.h"
#include "BattleUI.h"
#include "GridMap.h"
#include "input.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "Player.h"
#include "PlayerDataManager.h"
#include "Enemy.h"
#include "TurnManager.h"
#include "Hand.h"
#include "Deck.h"
#include "CardDataBase.h"
#include "CardEffect.h"
#include "SceneType.h"
#include "BattleHighlighter.h"
#include "CardExecutor.h"
#include "EncounterData.h"
#include "EncounterDataBase.h"
#include "BattleCamera.h"
#include "TimedQueue.h"

#include <vector>
#include <utility>
#include <functional>
#include <unordered_map>
#include <map>
#include <set>

enum class EnemyTurnPhase 
{
    WaitStart, 
    PoisonTick, 
    ProcessEnemy,
    DashStep,
    WaitAction, 
    NextEnemy, 
    EndTurn 
};

class BattleScene : public Scene
{
public:
    BattleScene();
    ~BattleScene();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void DrawImGui() override;
    const char* DebugName() const override { return "Battle"; }
    void HandleInput() override;

    int GetPlayerHp() const { return m_player ? m_player->GetHp() : 0; }
    int GetPlayerMaxHp() const { return m_player ? m_player->GetMaxHp() : 0; }

    BattleResult GetBattleResult() const { return m_battleResult; }

    bool GetCardRect(int i, float& x, float& y, float& w, float& h) const
    {
        return m_battleUI ? m_battleUI->GetCardRect(i, x, y, w, h) : false;
    }

    void DrawDropTooltipTop() { if (m_battleUI) m_battleUI->DrawDropTooltipTop(); }

    void GridToWorld(int col, int row, float& outX, float& outZ)
    {
        outX = (col - m_gridMap->GetCols() / 2.0f + 0.5f) * 1.1f;
        outZ = (row - m_gridMap->GetRows() / 2.0f + 0.5f) * 1.1f;
    }

    void SetEnemyId(const std::string& id) { m_battleEnemyId = id; }
    std::string GetEnemyId() const { return m_battleEnemyId; }

    std::function<void(SceneType)> onChangeScene;

    int m_battleSeed = 0;
    int m_overflow = 0;
    EncCategory m_category = EncCategory::Normal;
    void SetBattleSeed(int s) { m_battleSeed = s; }
    void SetOverflow(int o) { m_overflow = o; }
    void SetCategory(EncCategory c) { m_category = c; }

    int m_battleTier = 1;
    void SetTier(int t) { m_battleTier = t; }

    void SetHoveredCard(int i) { m_hoveredCardIndex = i; }

    void OnPlayerMoved(int fromCol, int fromRow, int moveDist);   // 移動時のレリック（from=移動前のマス）
    void OpenDiscardSelect(int n);                  // 「選んで捨てる」を開く（敵が全員倒れていたら開かない）
    void CancelDiscardSelect();                     // 開いていれば閉じる（勝敗が決まった時）

    std::vector<DropShown> m_dropResult;

    int m_prevHoveredCard = -1;

    void ApplyDiscardEffect(const CardEffectData& e);
    void AutoDiscardAll();

    void SetFreeLook(bool b) { m_freeLook = b; }
    void FreeLookStep(Input& in, float deltaTime);   // チュートリアル中の「見る系」駆動

    void SetForceHitmark(bool b) { m_forceHitmark = b; }
    void SetUiBlocked(bool b) { m_uiBlocked = b; }   // 上に窓（デッキ・マップ・設定など）が開いている間は、戦闘UIのホバーを止める
    bool GetHitmarkRect(float& x, float& y, float& w, float& h) const;
    bool GetEnemyPanelRect(float& x, float& y, float& w, float& h) const;

    void OnCardPlayed(const CardData* d);

    bool IsRelicSpent(const std::string& id) const;   // 一度きりレリックが使用済みか
    bool CamDemoCaption(const wchar_t*& text, const char*& icon, float& alpha) const;
    bool CamDemoActive() const { return m_camera.DemoActive(); }   // カメラ操作のデモ中か
    void OnTutorialClosed();   // 初戦チュートリアルを閉じた時（保留していたレリックを発動）

private:
    BattleUI* m_battleUI;
    GridMap* m_gridMap;
    Renderer3D* m_renderer3D;
    SpriteRenderer* m_spriteRenderer;
    ID3D11ShaderResourceView* m_whiteTexture;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    Input m_input;

    int m_playerCol;
    int m_playerRow;

    int m_hpStart = 0;   // 戦闘開始時HP（被ダメ算出用）

    int m_turnCount = 0;

    bool m_bossAwakened = false;   // 3層ボス：楔全滅で覚醒済みか
    bool m_bossAwakenPending = false;   // 次のボス手番を「覚醒ターン（何もしない＋演出）」にする
    int m_bossAwakenAtTurn = -1;   // この敵ターン以降に覚醒（-1=未予約）
    bool m_bossHadWedges = false;
    std::vector<std::pair<int, int>> m_wedgeCorners;   // 楔の四隅スロット（初期位置）
    void SwapWedges();                                 // 楔の位置交換（4ターン毎）
    float m_awakenCinematic = 0.0f;   // 覚醒演出の残り時間（>0で操作/ターン停止）
    float m_awakenRingTimer = 0.0f;   // 円を追加する間隔
    std::vector<float> m_awakenRings; // 拡大中の円の経過時間リスト
    void UpdateBossGimmick();      // 楔の生存でボス無敵/覚醒を制御
    void SyncFaithBuffs();         // 立っているマスの信仰を、聖地/聖域の加護バフとして付け外し
    void PurgeHandCurses();        // 浄化：手札の呪いを全て廃棄
    void BossInvulnAction(Enemy* boss);   // 無敵中の妨害・溜め（ダメージなし）

    TurnManager m_turnManager;

    BattleResult m_battleResult;

    HWND m_hWnd;
    int m_screenWidth;
    int m_screenHeight;

    Player* m_player;
    std::vector<Enemy*> m_enemies;
    Enemy* m_awakenBoss = nullptr;

    std::vector<std::string> m_defeatedEnemyIds;

    void AddEnemy(int col, int row, const std::string& id);
    void SummonNear(Enemy* src, const std::string& id, int count);

    Hand m_hand;
    int m_selectedCardIndex;
    int m_hoveredCardIndex;

    // 時間を置いて1つずつ進める処理（TimedQueue）。1件ごとのデータはその件の中に持つ
    struct PlayerHit { std::vector<Enemy*> targets; int damage = 0; CardEffectData onHit, onHit2; };   // 自分の連撃の追撃1発
    struct EnemyHit  { Enemy* enemy = nullptr; int damage = 0; bool last = false; };   // 敵の多段の1発（last＝最後の1発の後に状態異常）
    struct CoilTrap  { int c = 0, r = 0; Enemy* enemy = nullptr; };                      // 大蛇のとぐろが通ったマスの罠
    struct TrapFire  { int c = 0, r = 0; bool full = false; };                           // 起爆する罠（full＝周りにもそのまま）
    struct ChainWave { std::vector<std::pair<int, int>> cells; bool full = false; };     // 連鎖起爆の1つの波
    TimedQueue<PlayerHit> m_playerHits;
    TimedQueue<EnemyHit>  m_enemyHits;
    TimedQueue<CoilTrap>  m_coilTraps;
    TimedQueue<int>       m_startPoison;   // レリックの開始時の毒（量）
    TimedQueue<ChainWave> m_chain;
    TimedQueue<TrapFire>  m_detonate;
    void  StartCardSequences(const CardExecutor::ExecuteResult& r);   // カードを使った後の順番待ちを始める
    float StepPlayerHit(PlayerHit& h);
    float StepEnemyHit(EnemyHit& h);
    float StepCoilTrap(CoilTrap& t);
    float StepStartPoison(int& amount);
    float StepChainWave(ChainWave& w);
    float StepDetonate(TrapFire& f);
    static constexpr float MULTI_HIT_INTERVAL = 0.12f;

    Deck m_deck;
    static constexpr int HAND_SIZE = 7;
    int m_enemyAddedToHand = 0;   // 敵のターン中に手札へ入れられたお邪魔カードの枚数（引く枚数から除く）

    bool m_showDrawPile;
    bool m_showDiscardPile;
    bool m_showExhaustPile;

    bool m_firstAttackDone = false;

    int m_discardSelectCount = 0;
    std::vector<int> m_discardSelected;   // 選択中のカードindex
    bool m_discardViewMode = false;

    std::vector<std::pair<int, int>> m_dashStops;   // 止まるマス（罠マス＋最終マス）
    int    m_dashStopIdx = 0;
    Enemy* m_dashEnemy = nullptr;
    void   DashGlideTo(int c, int r);   // 距離に応じた時間で緩急つき移動

    EnemyTurnPhase m_enemyPhase = EnemyTurnPhase::WaitStart;
    int m_currentEnemyIdx = 0;
    int m_poisonIdx = 0;
    int m_poisonSubTicks = 0;
    float m_enemyActionDelay = 0;
    static constexpr float ENEMY_ACTION_PAUSE = 0.05f;
    static constexpr float ENEMY_BETWEEN_PAUSE = 0.4f;

    int m_decoyCol = -1, m_decoyRow = -1;

    std::pair<int, int> m_hoveredCell;

    float m_highlightTimer;
    float m_arrowRevealTimer = 0.0f;   // プレイヤーターン開始後、次矢印を出すまでの猶予

    void ProcessDeadEnemies();

    bool m_cardSelecting = false;
    CardEffectType m_selectingType = CardEffectType::None;

    std::string m_battleEnemyId;

    BattleHighlighter m_highlighter;
    CardExecutor      m_cardExecutor;

    BattleCamera m_camera;   // 位置・ズーム・手での操作・狙いカメラ・操作デモ

    // バフが付いた／増えた時のエフェクト（毎フレーム前の値と比べる＝どこで付いても拾える）
    std::unordered_map<const void*, std::map<int, int>> m_buffSnap;   // 持ち主 → バフ種類ごとの値（種類が増えても直さなくていいよう map）
    void UpdateBuffGainFx();

    void UpdateTrapsAtTurnStart();   // 自分の罠が育つ／敵が隣にいる間だけ導火線が進み、たまったら自分で起爆
    void  MaybeStartCamDemo();   // カメラ操作の自動デモを始めるか（条件はシーンの事情なので、ここで決める）

    bool SelectedNeedsTarget() const;                  // 選択中のカードがマスを選ぶ種類か（攻撃・移動・罠など）

    bool CellToScreen(int col, int row, float& sx, float& sy) const;   // マスの中心が今のカメラで画面のどこに映るか
    bool CellInCardRange(const CardData& d, int c, int r) const;   // カードの射程にマスが入るか（使用時・予測で共通）
    int  AttackTargetEnemy() const;   // 攻撃カードを今離したら当たる敵（無ければ-1）
    int  EnemyIndexAt(int c, int r) const;   // そのマスにいる敵の番号（いなければ-1。多マス敵・蛇の体も含む）
    bool AimsFreely(const CardData& d) const { return d.dash || d.rangeType == RangeType::Cone; }   // 空きマスを指す意味があるカード（突進先・扇の向き）
    int  FocusEnemy() const;          // 今注目している敵（範囲の点滅・選択枠・パネル強調で共通）
    const Enemy* AttackTarget() const { int i = AttackTargetEnemy(); return i >= 0 ? m_enemies[i] : nullptr; }
    POINT m_prevPathMouse = { -1, -1 };   // 前フレームのマウス位置（経路を変えるのはマウスが動いた時だけにする）


    // 描画（BattleSceneDraw.cpp）。Draw は描く順番を並べるだけ
    struct BattleTheme { const char* ground; const char* tex[3]; float w[3]; float h[3];
                         const char* cellTex; int cellVariants; XMFLOAT4 cellCol; };   // 地面・装飾3種(絵/幅/高さ)・マスの絵(variants>1 なら _0.._n-1)・空マスの色
    BattleTheme CurrentTheme() const;   // 層＋ボスで決まる背景テーマ
    void RequestCellCursor();
    void DrawBackground2D();
    void UpdateCellHeights();           // 狙うマスを浮かせる＋キャラの高さ（描く前に）
    void DrawGround(const BattleTheme& th);
    void DrawCells(const BattleTheme& th);
    void DrawFloorMarks();
    void DrawTraps();
    void DrawSnakes();
    void DrawUnits();
    void DrawOverlays();
    void DrawAwakenCinematic();
    BattleUIContext MakeUIContext();
    float CellWX(int col) const { return (col - m_gridMap->GetCols() / 2.0f) * 1.1f; }   // マス→ワールド座標（X）
    float CellWZ(int row) const { return (row - m_gridMap->GetRows() / 2.0f) * 1.1f; }   // マス→ワールド座標（Z）

    // 入力（BattleSceneInput.cpp）。HandleInput は振り分けだけ
    bool HandleBattleEndInput();      // 勝敗後のクリック。勝敗が付いていたら true
    void HandleDiscardSelectInput();  // 捨てるカードの選択中
    void PickEnemyOnGrid();           // 盤面の敵クリックで範囲を見る
    void UpdateHoveredCell();         // 指しているマス・狙い確定のタイマー
    void UpdateMovePath();            // 移動カードの経路づくり
    void HandleReleaseToPlay();       // 左ボタンを離したら使う
    void HandleClickInput();          // 山札・捨て札ボタン、一覧から選ぶ、クリックで使う
    void HandleHandInput();           // 手札を選ぶ・ホバー
    void HandleTurnEndAndPanel();     // ターン終了ボタン、敵パネル
    bool PlayCard(int targetCol, int targetRow, const std::vector<std::pair<int, int>>* usePath);   // カードを使う（2つの使い方の共通）
    void MovePlayerAfterCard(int newCol, int newRow, int moveDist, const std::vector<std::pair<int, int>>* usePath);   // 歩き・氷・地形
    std::vector<std::pair<int, int>> ShortestPath(std::pair<int, int> from, std::pair<int, int> to, int budget,
        const std::set<std::pair<int, int>>& blocked = {}) const;   // 空きマスだけの最短経路（見つからなければ空）
    static void ApplyKnifeThrow(CardData& d, const Player* p);       // ナイフ＋投擲術なら遠距離化

    float HiddenTargetsPanZ(const CardData& d) const;   // 手札に隠れるマスを手札の上に出すのに必要なカメラのずらし量

#ifdef _DEBUG
    int m_debugRank;
    int m_debugEncounterIndex;
#endif
    std::string m_currentEncounterId;  // 今のテンプレート情報を覚えておく

    bool m_pathBuilding = false;
    bool m_moveReleaseSuppress = false;
    std::vector<std::pair<int, int>> m_movePath;
    std::pair<int, int> m_backtrackCell = { -999, -999 };
    std::pair<int, int> m_suppressCell = { -999, -999 };
    int m_backtrackFrames = 0;

    int m_selectedEnemyRange = -1;

    std::string m_rewardRelic;

    void RunTurnCycle();   // 回転の輪：レリック値ぶん捨てて引く

    // 時間差ドロー（山札が尽きたらリシャッフル演出を挟む）
    int   m_drawSeqRemaining = 0;    // 残り何枚引くか
    float m_drawSeqTimer = 0.0f; // 次の1枚までの間隔
    float m_reshuffleFxTimer = 0.0f; // >0の間はリシャッフル演出中でドロー停止
    static constexpr float DRAW_SEQ_INTERVAL = 0.10f; // 1枚ごとの間隔（秒）
    void StartDrawSequence(int count);
    void OnDrawSequenceComplete();

    bool m_freeLook = false;

    bool m_forceHitmark = false;
    bool m_uiBlocked = false;      // SetUiBlocked

    struct Deco { float x, z, scale; int type; };   // type: 0=木 1=茂み 2=岩
    std::vector<Deco> m_decos;

    struct BurnHit { float atTime; int dmg; bool curse = false; };
    std::vector<BurnHit> m_burnHits;
    float m_burnClock = 0.0f;

    float m_windupTimer = 0.0f;     // メテオ落下待ち
    bool  m_windupPlayed = false;   // この行動の落下演出を出したか

    // 狙いの確定：離したマスに少し止まっていた時だけ使う（ドラッグ途中で通り過ぎたマスでの誤使用を防ぐ）
    static constexpr unsigned long long AIM_SETTLE_MS = 200;
    std::pair<int, int> m_aimCell = { -1, -1 };
    unsigned long long m_aimSince = 0;

    bool m_holdForTutorial = false;    // 初戦チュートリアルが閉じるまで、操作を求めるレリックを保留
    bool m_turnCyclePending = false;   // 保留中のturnCycle（閉じたら発動）
};