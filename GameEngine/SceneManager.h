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
#include "EventDataBase.h"

#include "TitleScene.h"
#include "BattleScene.h"
#include "CardSelectScene.h"
#include "FieldScene.h"
#include "ShopScene.h"
#include "ResultScene.h"


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

    void DrawBarTips();

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
    int CraftModSlots() const;

    void DrawEventRelicPicker();
    void GetEventRelicSlot(int i, float& x, float& y) const;
    int  EventRelicAt(POINT p) const;

private:
    Scene* m_currentScene;
    SceneType m_currentType;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    TextRenderer* m_textRenderer = nullptr;
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
    std::vector<int> VisibleDeckIndices() const;

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

    bool m_mapOpen = false;
    void DrawMap();

    bool m_restOpen = false;     // 3択画面 表示中
    bool m_restActive = false;   // 休憩の行動が未消費（サブ画面を開いている間もtrue）
    static constexpr int REST_HEAL = 20;
    void DrawRest();
    void HandleRestClick(POINT m);
    void FinishRest();
    void GetRestBtnRect(int i, float& x, float& y, float& w, float& h) const;

    float m_craftFxTimer = 0.0f;         // >0の間 演出再生
    std::string m_craftFxCard;           // 完成カードID
    static constexpr float CRAFT_FX_DURATION = 0.9f;
    void DrawCraftFx();

    void DrawRelicBar();
    void GetRelicRect(int i, float& x, float& y, float& w, float& h) const;
    std::string HoveredRelic(POINT mp) const;


    bool m_eventOpen = false;
    std::string m_eventId;
    int m_eventResult = -1;   // -1=選択中, >=0=結果表示
    void DrawEvent();
    void ApplyOutcomes(const EventChoice& c);
    void GetEventChoiceRect(int i, float& x, float& y, float& w, float& h) const;
    std::string m_eventPickType;   // "removeCard"/"upgradeCard"/"transformCard"（空=非ピッカー）
    int m_eventPickChoice = -1;
    void DrawEventPicker();
    void ApplyCardPick(int idx);
    void GetEventCardSlot(int i, float& x, float& y) const;
    int  EventCardAt(POINT p) const;
    int m_eventPickAnimIdx = -1;
    float m_eventPickAnimTimer = 0.0f;
    std::string m_eventPickAnimTo;   // 変化/強化先（空=削除の塵化）
    static constexpr float EVENT_PICK_ANIM_DUR = 0.7f;
    bool ChoiceEnabled(const EventChoice& c) const;
};