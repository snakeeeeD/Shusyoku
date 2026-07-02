#pragma once
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "Renderer3D.h"
#include "Player.h"
#include "Enemy.h"
#include "Hand.h"
#include "Deck.h"
#include "TurnManager.h"
#include "GridMap.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "BattleHighlighter.h"
#include "TextureManager.h"
#include <vector>
#include <utility>
#include <string>

enum class BattleResult
{
    None,
    Win,
    Lose,
};

struct DrawCardEffect
{
    std::string cardId;
    float x, y;
    float targetX, targetY;
    float alpha;
    float timer;
    bool  done;
};

struct DiscardCardEffect {
    float startX, startY;
    float alpha;
    float timer;
    bool done;
};


struct CardAnimState {
    float currentX, currentY;
};

struct PlayCardEffect {
    float startX, startY;
    float alpha;
    float timer;
    bool done;
    CardType cardType;
};

struct HPBarInfo
{
    int currentHP;
    int maxHP;
    float displayHP;
    int block;
    int poisonDmg;
    bool hasBurn;
};

struct BattleUIContext
{
    Player* player;
    std::vector<Enemy*>* enemies;
    Hand* hand;
    Deck* deck;
    TurnManager* turnManager;
    Renderer3D* renderer3D;
    GridMap* gridMap;

    int playerCol, playerRow;
    int selectedCardIndex;
    int hoveredCardIndex;
    std::pair<int, int> hoveredCell;
    float highlightTimer;
    BattleResult battleResult;
    POINT mousePos;

    bool showDrawPile;
    bool showDiscardPile;
    bool isPlayerTurn;
    float cameraZoom;

    int screenWidth, screenHeight;
};

class BattleUI
{
public:
    ~BattleUI();
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, IDXGISwapChain* swapChain);

    void Draw(const BattleUIContext& ctx);

    void UpdateDrawCardEffects(float deltaTime);
    void StartDrawCardEffect(const std::string& cardId);
    void StartDiscardEffects();
    void UpdateDiscardEffects(float deltaTime);
    void UpdateCardAnimations(float deltaTime, int handSize, int hoveredIndex, 
        int selectedIndex, POINT mousePos, bool selectedNeedsTarget);
    void OnCardRemoved(int index);
    void StartPlayCardEffect(CardType type, float fromX, float fromY);
    void UpdatePlayCardEffects(float deltaTime);

    void ClearCardAnimations() { m_cardAnims.clear(); }

    TextRenderer* GetTextRenderer() { return m_textRenderer; }

private:
    SpriteRenderer* m_spriteRenderer = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    ID3D11ShaderResourceView* m_whiteTexture = nullptr;

    int m_screenWidth = 0;
    int m_screenHeight = 0;

    int m_panelHoveredEnemy = -1;

    std::vector<DrawCardEffect> m_drawCardEffects;
    std::vector<PlayCardEffect> m_playCardEffects;

    static constexpr float CARD_WIDTH = 100.0f;
    static constexpr float CARD_HEIGHT = 110.0f;
    static constexpr float CARD_HIDE_Y_OFFSET = 30.0f;
    static constexpr float CARD_HOVER_Y_OFFSET = 60.0f;
    static constexpr float CARD_HOVER_W = 110.0f;
    static constexpr float CARD_HOVER_H = 140.0f;
    static constexpr float DRAW_EFFECT_DURATION = 0.4f;

    void DrawHPBar(float x, float y, float w, float h, const HPBarInfo& info, float time);
    void DrawEnemyHPBar(Enemy* enemy, Renderer3D* renderer3D);
    void DrawEnemyInfoPanel(const BattleUIContext& ctx);
    void DrawTargetIndicators(const BattleUIContext& ctx);
    void DrawArrowIndicator(float sx, float sy, const DirectX::XMFLOAT4& color, float highlightTimer);
    void DrawPileViewer(const BattleUIContext& ctx);
    void DrawCardEffects();
    std::vector<CardAnimState> m_cardAnims;
    std::vector<DiscardCardEffect> m_discardCardEffects;
    void DrawPlayCardEffects();
    void DrawDiscardEffects();
    void DrawPlayerOffScreenIndicator(const BattleUIContext& ctx);
    void DrawEnemyGridHighlight(const BattleUIContext& ctx);

    BuffType m_hoveredBuffType = BuffType::AttackUp;
    int m_hoveredBuffEnemy = -1;
    int m_hoveredBuffValue = 0;
    float m_hoveredBuffX = 0;
    float m_hoveredBuffY = 0;
    bool m_hasHoveredBuff = false;
    bool m_cardLockedToCenter = false;

    bool GetEnemyScreenPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const;
    bool GetEnemyFootPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const;
    std::wstring GetCardEffectText(const CardData* data, Player* player) const;
    bool IsCardBoosted(const CardData* data, Player* player) const;

    static void GridToWorld(GridMap* gridMap, int col, int row, float& outX, float& outZ);
};