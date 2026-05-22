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
#include "SceneType.h"

#include <vector>
#include <utility>
#include <functional>

enum class BattleResult
{
    None,   // 進行中
    Win,    // 勝利
    Lose,   // 敗北
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

    int m_prevHoveredCardIndex; // 前フレームのホバー状態

    Deck m_deck;
    static constexpr int HAND_SIZE = 7; // 毎ターン引く枚数

    static constexpr float ENEMY_HPBAR_OFFSET_Y = 1.2f; // 頭上の高さ

    float m_enemyTurnTimer;
    static constexpr float ENEMY_TURN_DELAY = 5.5f; // 0.5秒待つ

    // ハイライト中のマスリスト
    std::vector<std::pair<int, int>> m_highlightCells;
    std::pair<int, int> m_hoveredCell; // マウスが乗っているマス

    // ハイライト計算関数
    void UpdateHighlight(int centerCol, int centerRow, const CardData* data);
    void ClearHighlight();

    float m_highlightTimer;  // ハイライト明滅用タイマー

    std::wstring GetCardEffectText(const CardData* data) const;
    bool IsCardBoosted(const CardData* data) const;

    // Enemyの死亡判定用
    void ProcessDeadEnemies();

    
    std::vector<std::pair<int, int>> m_enemyHighlightCells;
    void UpdateEnemyIntentHighlight();
    void ClearEnemyHighlight();
};