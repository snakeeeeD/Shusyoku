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
#include "Telemetry.h"

#include "TitleScene.h"
#include "BattleScene.h"
#include "CardSelectScene.h"
#include "FieldScene.h"
#include "ShopScene.h"
#include "ResultScene.h"

struct URect { float x, y, w, h; bool has(POINT p) const { return p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h; } };
struct SettingsUI { URect panel, disp, bgmTrack, seTrack, shake, gameEnd, close, gear; };

class SceneManager
{
public:
    SceneManager();
    ~SceneManager();

    static const char* SceneName(SceneType t) {
        switch (t) {
        case SceneType::Title:      return "title";
        case SceneType::Battle:     return "battle";
        case SceneType::CardSelect: return "cardselect";
        case SceneType::Field:      return "field";
        case SceneType::Shop:       return "shop";
        case SceneType::Result:     return "result";
        default:                    return "other";
        }
    }

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd,
        IDXGISwapChain* swapChain);

    void ChangeScene(SceneType type);

    enum class Fade { None, Out, In };
    Fade m_fadeState = Fade::None;
    float m_fadeAlpha = 0.0f;                 // 1=真っ黒, 0=透明
    SceneType m_pendingScene = SceneType::Title;
    static constexpr float FADE_SPEED = 3.0f; // 1秒あたり（約0.33秒で完了）
    void DoChangeScene(SceneType type);       // 実際の切替

    void Update(float deltaTime);
    void Draw();
    void DrawImGui();
    void HandleInput();

    struct TutorialPage { std::wstring text; float hx = 0, hy = 0, hw = 0, hh = 0; 
    int hoverCard = -1; bool stepsTip = false; bool showHitmark = false; };
    std::vector<TutorialPage> m_tutorialPages;
    int m_tutorialPage = 0;
    bool m_tutorialOpen = false;
    void ShowTutorial(const std::vector<TutorialPage>& pages);
    void DrawTutorial();
    std::vector<TutorialPage> m_pendingTutorial;
    float m_tutorialDelay = 0.0f;
    void ShowTutorialDelayed(const std::vector<TutorialPage>& pages, float delay);

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

    std::string m_generalBgm;
    int m_generalBgmLayer = -1;
    void PlayGeneralBGM();

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
    static constexpr float EVENT_PICK_ANIM_DUR = 2.0f;
    bool ChoiceEnabled(const EventChoice& c) const;

    enum class EventPending { None, Battle, Shop, Treasure, CardSelect };
    EventPending m_eventPending = EventPending::None;
    std::string  m_eventBattleParam;
    std::string  m_cardSelectMode; 

    void DrawBigCard(const CardData* d, float x, float y, float scale, bool textPass);

    int  m_deckHoveredPrev = -1;
    int  m_deckPreviewIdx = -1;   // クリックで拡大中のカード（-1=なし）
    void DrawDeckPreview();
    bool m_deckShowUpgrade = false;   // デッキ閲覧時に強化後も表示するか
    bool m_previewShowUpgrade = false;   // 拡大表示中に+版を見せるか
    bool m_upgradeBtnHov = false;

    bool m_settingsOpen = false;
    int  m_dragSlider = -1;               // 0=BGM,1=SE,-1=なし
    void DrawSettings();
    SettingsUI SettingsLayout() const
    {
        SettingsUI u{};
        u.gear = { (float)m_screenWidth - 58.0f, 5.0f, 48.0f, 30.0f };
        float pw = 440.0f, ph = 348.0f;
        float px = (m_screenWidth - pw) / 2.0f, py = (m_screenHeight - ph) / 2.0f;
        u.panel = { px, py, pw, ph };
        u.disp = { px + 180.0f, py + 54.0f, 200.0f, 34.0f };
        u.bgmTrack = { px + 180.0f, py + 120.0f, 200.0f, 14.0f };
        u.seTrack = { px + 180.0f, py + 168.0f, 200.0f, 14.0f };
        u.shake = { px + 180.0f, py + 208.0f, 120.0f, 34.0f };
        u.gameEnd = { px + pw / 2.0f - 90.0f, py + 250.0f, 180.0f, 34.0f };
        u.close = { px + pw / 2.0f - 70.0f, py + ph - 52.0f, 140.0f, 38.0f };
        return u;
    }
};