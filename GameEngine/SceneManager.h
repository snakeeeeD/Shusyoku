#pragma once
#include "Scene.h"
#include <memory>
#include <string> 
#include <vector>
#include "Scenetype.h"
#include "TextRenderer.h"
#include "SpriteRenderer.h"
#include "input.h"
#include "TextureManager.h"
#include "MaterialDataBase.h"
#include "CardDataBase.h"

#include "TitleScene.h"
#include "BattleScene.h"
#include "CardSelectScene.h"
#include "FieldScene.h"
#include "ShopScene.h"


class SceneManager
{
public:
    SceneManager();
    ~SceneManager();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd,
        IDXGISwapChain* swapChain);

    void ChangeScene(SceneType type);

    void Update(float deltaTime);
    void Draw();
    void DrawImGui();
    void HandleInput();

    void DrawCraft();
    void GetCraftSlotRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftInvRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftBtnRect(float& x, float& y, float& w, float& h) const;
    std::vector<std::pair<std::string, int>> CraftInventory() const;
    void GetCraftBaseRect(int i, float& x, float& y, float& w, float& h) const;
    void GetCraftModRect(int i, float& x, float& y, float& w, float& h) const;
    std::vector<std::pair<std::string, int>> CraftBases() const;
    std::vector<std::pair<std::string, int>> CraftMods() const;
    std::string CraftRecipeId() const;

private:
    Scene* m_currentScene;
    SceneType m_currentType;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    TextRenderer* m_textRenderer;
    IDXGISwapChain* m_swapChain;

    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;

    SpriteRenderer* m_uiSprite = nullptr;
    Input m_uiInput;
    bool  m_deckOpen = false;

    static constexpr float BAR_H = 40.0f;

    float m_deckScroll = 0.0f;

    float m_uiTime = 0.0f;

    void DrawOverlay();
    void DrawDeckCards(bool textPass);

    bool m_deckRemoveMode = false;
    static constexpr float DECK_SCALE = 1.1f;

    bool GetDeckCardBase(int i, float& baseX, float& baseY) const;   // カードの基準位置
    int  GetDeckCardAt(POINT p) const;                              // 座標→デッキindex

    bool m_deckUpgradeMode = false;

    bool m_craftOpen = false;
    std::string m_craftBase;
    std::vector<std::string> m_craftMods;   // 最大2
    void HandleCraftClick(POINT m);
    void DoCraft();

    bool m_invOpen = false;
    void DrawInventory();
    void DrawItemTooltip(const std::string& id, POINT mp);
    std::string HoveredItem(POINT mp) const;
};