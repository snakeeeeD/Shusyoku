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
#include "ItemTooltip.h"
#include "BattleHighlighter.h"
#include "TextureManager.h"
#include "FloatingText.h"
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
    float startScale = 1.0f;
    float startRot = 0.0f;
    float alpha;
    float timer;
    bool done;
    CardType cardType = CardType::Skill;   // 種別（色表示用）
    float delay = 0.0f;                     // 開始遅延（過負荷の先出し用）
    const CardData* data = nullptr;
    bool  fromCenter = false;               // 中央スタート（過負荷）
};


struct CardAnimState {
    float currentX, currentY;
    float currentScale = 1.0f;
    float currentRot = 0.0f;
};

struct PlayCardEffect {
    float startX, startY;
    float startScale = 1.0f;
    float startRot = 0.0f;
    float alpha;
    float timer;
    bool done;
    CardType cardType;
    const CardData* data = nullptr;
    float delay = 0.0f;
    bool isBurn = false;    // 過負荷専用モーション（ため＋メリハリ）
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

struct DropShown 
{
    std::string id;
    int count;
    bool rare; 
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
    bool showExhaustPile;
    bool isPlayerTurn;
    bool showMoveArrows = true;
    bool cardSelecting = false;
    float cameraZoom;

    int screenWidth, screenHeight;

    const std::vector<std::pair<int, int>>* outOfRangeCells = nullptr;
    const std::vector<std::pair<int, int>>* travelPath = nullptr;

    int selectedEnemy = -1;

    int discardSelectCount = 0;
    const std::vector<int>* discardSelected = nullptr;
    bool discardViewMode = false;

    const std::vector<DropShown>* drops = nullptr;

    const std::string* rewardRelic = nullptr;
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
    void StartReshuffleEffect();
    void UpdateReshuffleEffect(float deltaTime);
    void DrawReshuffleEffect();
    void StartDiscardEffects();
    void StartOverflowDiscardEffect();
    void UpdateDiscardEffects(float deltaTime);
    void UpdateCardAnimations(float deltaTime, int handSize, int hoveredIndex,
        int selectedIndex, POINT mousePos, bool selectedNeedsTarget,
        const std::vector<int>* discardSelected = nullptr);
    void OnCardRemoved(int index);
    void UpdatePlayCardEffects(float deltaTime);

    void ClearCardAnimations() { m_cardAnims.clear(); }

    void StartPlayCardEffect(CardType type, int cardIndex);
    void StartPlayCardEffect(const CardData* data, int cardIndex);
    void StartPlayCardEffect(CardType type, float fromX, float fromY);
    void StartPlayCardEffect(const CardData* data, float startX, float startY, float delay = 0.0f, bool isBurn = false);

    TextRenderer* GetTextRenderer() { return m_textRenderer; }
    int GetPanelHoveredEnemy() const { return m_panelHoveredEnemy; }

    int GetCardAtScreenPos(POINT p) const;

    int GetCardAnimCount() const { return (int)m_cardAnims.size(); }

    bool GetCardRect(int i, float& x, float& y, float& w, float& h) const
    {
        if (i < 0 || i >= (int)m_cardAnims.size()) return false;
        CardVisual::GetRect(m_cardAnims[i].currentX, m_cardAnims[i].currentY,
            m_cardAnims[i].currentScale, x, y, w, h);
        return true;
    }

    void GetDiscardConfirmRect(float& x, float& y, float& w, float& h) const;
    void GetDiscardViewRect(float& x, float& y, float& w, float& h) const;
    bool IsOnDiscardConfirm(POINT p) const;
    bool IsOnDiscardView(POINT p) const;
    void StartDiscardEffectAt(int cardIndex, const CardData* data = nullptr, float delay = 0.0f);
    void StartBurnDiscard(const CardData* data, float delay);
    void StartPlayCardEffectFromHand(const CardData* data, int cardIndex, float delay = 0.0f, bool isBurn = false);

    void DrawUnitStatusSprites(float footX, float footY, float scale, bool isBoss,
        const HPBarInfo& bar, BuffManager& bm, POINT mousePos, float timer);
    void DrawUnitStatusText(float footX, float footY, float scale, bool isBoss,
        const HPBarInfo& bar, BuffManager& bm);

    void DrawDropTooltipTop();

    bool GetHitmarkRect(Enemy* enemy, Renderer3D* renderer3D, float& x, float& y, float& w, float& h) const;

    static constexpr float BURN_EFFECT_DUR = 0.75f;   // 過負荷は長め（ため time込み）

private:
    SpriteRenderer* m_spriteRenderer = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    ID3D11ShaderResourceView* m_whiteTexture = nullptr;

    int m_screenWidth = 0;
    int m_screenHeight = 0;

    int m_panelHoveredEnemy = -1;

    std::vector<DrawCardEffect> m_drawCardEffects;
    std::vector<PlayCardEffect> m_playCardEffects;

    struct ReshuffleFx { bool active = false; float timer = 0.0f; };
    ReshuffleFx m_reshuffleFx;
    static constexpr float RESHUFFLE_FX_DUR = 0.6f;   // BattleScene側と合わせる

    static constexpr float CARD_WIDTH = 100.0f;
    static constexpr float CARD_HEIGHT = 140.0f;
    static constexpr float CARD_HIDE_Y_OFFSET = 120.0f;
    static constexpr float CARD_HOVER_Y_OFFSET = 60.0f;
    static constexpr float CARD_HOVER_W = 110.0f;
    static constexpr float CARD_HOVER_H = 140.0f;
    static constexpr float DRAW_EFFECT_DURATION = 0.25f;

    void DrawHPBar(float x, float y, float w, float h, const HPBarInfo& info, float time);
    void DrawEnemyHPBar(Enemy* enemy, Renderer3D* renderer3D);
    void DrawEnemyInfoPanel(const BattleUIContext& ctx);
    void DrawEnemyKeywords(const BattleUIContext& ctx);
    void DrawTargetIndicators(const BattleUIContext& ctx);
    void DrawArrowIndicator(float sx, float sy, const DirectX::XMFLOAT4& color, float highlightTimer);
    void DrawPileViewer(const BattleUIContext& ctx);
    void DrawCardEffects();
    std::vector<CardAnimState> m_cardAnims;
    std::vector<DiscardCardEffect> m_discardCardEffects;
    void DrawPlayCardEffects();
    void DrawDiscardEffects();
    void DrawPlayerOffScreenIndicator(const BattleUIContext& ctx);
    void DrawWindow(float x, float y, float w, float h,
        const DirectX::XMFLOAT4& tint = DirectX::XMFLOAT4(1, 1, 1, 1));
    void DrawCardKeywords(const BattleUIContext& ctx, int idx);


    BuffType m_hoveredBuffType = BuffType::AttackUp;
    int m_hoveredBuffEnemy = -1;
    int m_hoveredBuffValue = 0;
    float m_hoveredBuffX = 0;
    float m_hoveredBuffY = 0;
    bool m_hasHoveredBuff = false;
    bool m_cardLockedToCenter = false;

    bool GetEnemyScreenPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const;
    bool GetEnemyFootPos(Enemy* enemy, Renderer3D* renderer3D, float& outX, float& outY) const;

    static void GridToWorld(GridMap* gridMap, int col, int row, float& outX, float& outZ);
    void GetPlayEffectTransform(const PlayCardEffect& e, float& x, float& y, float& scale, float& rot);
    void DrawPlayCardEffectsFull(const BattleUIContext& ctx);
    void DrawDiscardEffectTexts(const BattleUIContext& ctx);
    void DrawDiscardEffectsFull(const BattleUIContext& ctx);

    static constexpr float PLAY_EFFECT_DUR = 0.45f;
    static constexpr float DISCARD_EFFECT_DUR = 0.55f;

    bool WorldToScreen(float wx, float wy, float wz, Renderer3D* renderer3D,
        float& outX, float& outY) const;
    void DrawFloatingTexts(const BattleUIContext& ctx);

    // 敵インテントアイコンのホバー説明
    bool m_intentHover = false;
    std::wstring m_intentTitle, m_intentBody;
    float m_intentX = 0.0f, m_intentY = 0.0f;

    std::string m_hoverDropId;
    float m_hoverDropX = 0.0f, m_hoverDropY = 0.0f;
};