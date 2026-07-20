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

#include <vector>
#include <utility>
#include <functional>

enum class EnemyTurnPhase {
    WaitStart,
    ProcessEnemy,
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
    void HandleInput() override;

    void GridToWorld(int col, int row, float& outX, float& outZ)
    {
        outX = (col - m_gridMap->GetCols() / 2.0f + 0.5f) * 1.1f;
        outZ = (row - m_gridMap->GetRows() / 2.0f + 0.5f) * 1.1f;
    }

    void SetEnemyId(const std::string& id) { m_battleEnemyId = id; }

    std::function<void(SceneType)> onChangeScene;

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

    int m_turnCount = 0;

    TurnManager m_turnManager;

    BattleResult m_battleResult;

    HWND m_hWnd;
    int m_screenWidth;
    int m_screenHeight;

    Player* m_player;
    std::vector<Enemy*> m_enemies;

    void AddEnemy(int col, int row, const std::string& id);

    Hand m_hand;
    int m_selectedCardIndex;
    int m_hoveredCardIndex;

    // カード表示定数（HandleInputで使用）
    static constexpr float CARD_WIDTH = 100.0f;
    static constexpr float CARD_HEIGHT = 110.0f;
    static constexpr float CARD_HIDE_Y_OFFSET = 30.0f;
    static constexpr float CARD_HOVER_Y_OFFSET = 60.0f;

    int m_prevHoveredCardIndex;

    Deck m_deck;
    static constexpr int HAND_SIZE = 7;

    bool m_showDrawPile;
    bool m_showDiscardPile;
    bool m_showExhaustPile;
    bool m_rightClickDragged;

    EnemyTurnPhase m_enemyPhase = EnemyTurnPhase::WaitStart;
    int m_currentEnemyIdx = 0;
    float m_enemyActionDelay = 0;
    static constexpr float ENEMY_ACTION_PAUSE = 0.05f;
    static constexpr float ENEMY_BETWEEN_PAUSE = 0.4f;

    std::pair<int, int> m_hoveredCell;

    float m_highlightTimer;

    void ProcessDeadEnemies();

    bool m_cardSelecting = false;
    CardEffectType m_selectingType = CardEffectType::None;

    std::string m_battleEnemyId;

    BattleHighlighter m_highlighter;
    CardExecutor      m_cardExecutor;

    // カメラ
    float m_cameraZoom;
    static constexpr float ZOOM_MIN = 0.5f;
    static constexpr float ZOOM_MAX = 0.8f;
    static constexpr float ZOOM_SPEED = 0.1f;

    bool m_isDraggingCamera;
    POINT m_dragStartPos;
    float m_cameraOffsetX;
    float m_cameraOffsetZ;

    int m_debugRank;
    int m_debugEncounterIndex;
    std::string m_currentEncounterId;  // 今のテンプレート情報を覚えておく

    bool m_pathBuilding = false;
    bool m_moveReleaseSuppress = false;
    std::vector<std::pair<int, int>> m_movePath;
    std::pair<int, int> m_backtrackCell = { -999, -999 };
    std::pair<int, int> m_suppressCell = { -999, -999 };
    int m_backtrackFrames = 0;

    int m_selectedEnemyRange = -1;
};