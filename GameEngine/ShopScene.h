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
    struct ShopItem { std::string id; int price; bool bought; };
    void GenerateStock();
    int  PriceFor(const CardData* data) const;
    std::wstring GetCardEffectText(const CardData* data) const;

    SpriteRenderer* m_spriteRenderer = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    Input m_input;
    ID3D11ShaderResourceView* m_whiteTexture = nullptr;

    int m_screenWidth = 0, m_screenHeight = 0;
    HWND m_hWnd = nullptr;

    std::vector<ShopItem> m_items;
    int  m_hoveredIndex = -1;
    bool m_readyForInput = false;

    static constexpr int   STOCK_COUNT = 4;
    static constexpr float CARD_W = 150.0f;
    static constexpr float CARD_H = 200.0f;
};