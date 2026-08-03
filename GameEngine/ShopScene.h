#pragma once
#include "Scene.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "TextureManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "input.h"
#include "Scenetype.h"
#include <vector>
#include <string>
#include <functional>

enum class ShopKind
{
    Card,
    Core,
    Material,
    Relic
};

class ShopScene : public Scene
{
public:
    ShopScene();
    ~ShopScene();
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    std::function<void(SceneType)> onChangeScene;

private:
    struct ShopItem { std::string id; int price; bool bought; ShopKind kind; };
    void GenerateStock();
    void GetSlotBase(int i, float& cardX, float& baseY) const;
    int  PriceFor(const CardData* data) const;

    SpriteRenderer* m_spriteRenderer = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    Input m_input;
    ID3D11ShaderResourceView* m_whiteTexture = nullptr;

    int m_screenWidth = 0, m_screenHeight = 0;
    HWND m_hWnd = nullptr;

    std::vector<ShopItem> m_items;
    int  m_hoveredIndex = -1;
    float m_time = 0.0f;
    bool m_readyForInput = false;

    static constexpr int   CARD_COUNT = 3;
    static constexpr int   CORE_COUNT = 1;
    static constexpr int   MAT_COUNT = 2;
    static constexpr int   STOCK_COUNT = CARD_COUNT + CORE_COUNT + MAT_COUNT;
    static constexpr int   CORE_PRICE = 120;
    static constexpr int   MAT_PRICE = 40;
    static constexpr float CARD_W = 150.0f;
    static constexpr float CARD_H = 200.0f;

    static constexpr float SHOP_SCALE = 1.4f;

    static constexpr int RELIC_PRICE = 160;

    static constexpr int RELIC_STOCK = 3;

    static constexpr int REMOVE_BASE = 75;
    static constexpr int REMOVE_STEP = 25;   // 1回ごとの値上げ

    bool m_removeMode = false;
    bool m_removedThisShop = false;          // このショップで削除済みか
    void DrawDeckRemoval();
    void GetDeckSlot(int i, float& x, float& y) const;
    int  DeckCardAt(POINT p) const;
    int RemovePrice() const;

    bool m_sellMode = false;
    void DrawSellMode();
    std::vector<std::string> OwnedItemIds() const;
    void GetSellSlot(int i, float& x, float& y) const;
    int  SellItemAt(POINT p) const;

    int m_prevHovered = -1;
};