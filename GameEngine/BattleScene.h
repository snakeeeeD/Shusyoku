#pragma once
#include "Scene.h"
#include "SpriteRenderer.h"
#include "GridMap.h"
#include "input.h"
#include "Renderer3D.h"
#include"Texturemanager.h"
#include "TextRenderer.h"
#include "Player.h"
#include "PlayerDataManager.h"
#include"Enemy.h"
#include "TurnManager.h"
#include "Hand.h"
#include "Deck.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardEffect.h"
#include "SceneType.h"
#include "BattleHighlighter.h"
#include "CardExecutor.h"

#include <vector>
#include <utility>
#include <functional>

enum class BattleResult
{
    None,   // 進行中
    Win,    // 勝利
    Lose,   // 敗北
};

struct DrawCardEffect
{
    std::string cardId;
    float x, y;         // 現在位置
    float targetX, targetY; // 目標位置
    float alpha;        // 透明度
    float timer;        // 経過時間
    bool  done;         // 完了フラグ
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
    void HandleInput() override;

    void GridToWorld(int col, int row, float& outX, float& outZ)
    {
        outX = (col - m_gridMap->GetCols() / 2.0f + 0.5f) * 1.1f;
        outZ = (row - m_gridMap->GetRows() / 2.0f + 0.5f) * 1.1f;
    }

    void DrawHPBar(float x, float y, float width, float height,
        int currentHP, int maxHP);

    void DrawEnemyHPBar(Enemy* enemy);

    void DrawPileViewer();  // 山札、捨て札の中身表示

    void SetEnemyId(const std::string& id) { m_battleEnemyId = id; }

    std::function<void(SceneType)> onChangeScene;

private:
    SpriteRenderer* m_spriteRenderer;
    TextRenderer* m_textRenderer;
    GridMap* m_gridMap;
    Renderer3D* m_renderer3D;
    ID3D11ShaderResourceView* m_whiteTexture;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    Input m_input;

    int m_playerCol;
    int m_playerRow;

    TurnManager m_turnManager;

    BattleResult m_battleResult;

    HWND m_hWnd;
    int m_screenWidth;
    int m_screenHeight;

    Player* m_player;
    std::vector<Enemy*> m_enemies;

    Hand m_hand;
    int m_selectedCardIndex;    // 選択中のカードのインデックス
    int m_hoveredCardIndex;     // マウスが乗っているカードのインデックス

    // カード表示定数
    static constexpr float CARD_WIDTH = 100.0f;
    static constexpr float CARD_HEIGHT = 110.0f;
    static constexpr float CARD_HIDE_Y_OFFSET = 30.0f;
    static constexpr float CARD_HOVER_Y_OFFSET = 60.0f;
    static constexpr float CARD_HOVER_W = 110.0f;
    static constexpr float CARD_HOVER_H = 140.0f;

    // 山札、捨て札表示定数
    static constexpr float DRAW_PILE_BTN_X = 20.0f;
    static constexpr float DRAW_PILE_BTN_Y = 660.0f; // m_screenHeight - 60
    static constexpr float DISCARD_BTN_X = 80.0f;
    static constexpr float DISCARD_BTN_Y = 660.0f;
    static constexpr float PILE_BTN_W = 50.0f;
    static constexpr float PILE_BTN_H = 40.0f;

    int m_prevHoveredCardIndex; // 前フレームのホバー状態

    Deck m_deck;
    static constexpr int HAND_SIZE = 7; // 毎ターン引く枚数

    // 山札、捨て札UI
    bool m_showDrawPile;        // 山札表示中か
    bool m_showDiscardPile;     // 捨て札表示中か

    static constexpr float ENEMY_HPBAR_OFFSET_Y = 1.2f; // 頭上の高さ

    float m_enemyTurnTimer;
    static constexpr float ENEMY_TURN_DELAY = 5.5f;

    std::pair<int, int> m_hoveredCell; // マウスが乗っているマス

    float m_highlightTimer;  // ハイライト明滅用タイマー

    std::wstring GetCardEffectText(const CardData* data) const;
    bool IsCardBoosted(const CardData* data) const;

    std::vector<DrawCardEffect> m_drawCardEffects;
    static constexpr float DRAW_EFFECT_DURATION = 0.4f;

    void StartDrawCardEffect(const std::string& cardId);
    void UpdateDrawCardEffects(float deltaTime);
    void DrawCardEffects();

    // Enemyの死亡判定用
    void ProcessDeadEnemies();
    
    std::string m_battleEnemyId;

    BattleHighlighter m_highlighter;
    CardExecutor      m_cardExecutor;
};