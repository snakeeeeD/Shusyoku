#pragma once
#include "Scene.h"
#include "HoverLift.h"
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
#include "EncounterEditor.h"

#include "TitleScene.h"
#include "BattleScene.h"
#include "CardSelectScene.h"
#include "FieldScene.h"
#include "ShopScene.h"
#include "ResultScene.h"
#include "URect.h"
#include "ScrollList.h"
#include "UiContext.h"
#include "GlossaryPanel.h"
#include "SettingsPanel.h"
#include "Tutorials.h"     // TutorialPanel と、チュートリアルの中身の表
#include "EventPanel.h"
#include "ItemPanel.h"
#include "DeckPanel.h"
#include "RestPanel.h"
#include "MapPanel.h"
#include "BossRelicPanel.h"

#include <functional>
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
        case SceneType::QuickSelect: return "quickselect";
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

    void ChangeScene(SceneType type, std::function<void()> onBlack);  // onBlackを真っ黒の瞬間に実行
    std::function<void()> m_onBlack;                                  // 暗転完了時に一度だけ実行

    void Update(float deltaTime);
    void Draw();
    void DrawImGui();
    void HandleInput();

    bool CamDemoActive() const;   // 今の戦闘でカメラ操作のデモ中か（ヒントを止めて隠す）
    void ShowTutorialOnce(TutorialId id);   // 初めての時に1回だけ（中身は Tutorials.h）
    URect TutorialSpot(const TutorialPanel::Page& pg) const;   // 照らす場所（戦闘の物なら今の位置）

    void DrawBarTips();


    void  GetBackBtnRect(float& x, float& y, float& w, float& h) const;
    bool  BackBtnHit(POINT m) const;
    void  DrawBackButton();

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

    static constexpr float BAR_H = UiContext::BAR_H;   // 上の帯の高さ（窓と共通）



    bool m_mDrag = false;
    int  m_mDragStartY = 0;
    int  m_mDragLastY = 0;
    bool m_mDragMoved = false;   // 閾値以上動いた＝ドラッグ扱い（選択を抑制）

    float m_uiTime = 0.0f;

    void DrawOverlay();





    void DrawItemTooltip(const std::string& id, POINT mp);


    void FinishRest();   // 休憩を終える（休憩・デッキ・合成を閉じて保存）

    float m_craftFxTimer = 0.0f;         // >0の間 演出再生
    std::string m_craftFxCard;           // 完成カードID
    static constexpr float CRAFT_FX_DURATION = 0.9f;
    void DrawCraftFx();
    std::vector<std::string> m_craftFxQueue;   // 続けて見せるカード（呪いを2枚もらう時など）
    void PlayNextCardFx();                     // 列の先頭のカードを出現演出で見せる

    void DrawRelicBar();
    void GetRelicRect(int i, float& x, float& y, float& w, float& h) const;
    std::string HoveredRelic(POINT mp) const;


    // ボスレリックの選択（候補があるフィールドで開くモーダル）
    bool BossRelicPickOpen() const;
    bool m_eatRelease = false;   // 確認の窓で押したクリックの「離す」を、閉じた後の画面に渡さない
    std::string  m_cardSelectMode;   // 次のカード報酬のモード（イベントから） 



    bool m_swallowSceneClick = false;   // モーダルを閉じたクリックが下のシーンに漏れないよう、ボタンを離すまでシーンの入力を止める
    // 用語集の窓（Glossary::RequestOpen でどこからでも開ける）
    GlossaryPanel m_glossary;
    // 設定の窓（帯のギア・タイトルの左下から）
    SettingsPanel m_settings;
    // ページ型のチュートリアル
    TutorialPanel m_tutorial;
    // イベントの窓（フィールドのイベントマス・エディタから）
    EventPanel m_event;
    // アイテムの窓（持ち物と合成）
    ItemPanel m_items;
    // デッキの窓（帯の「デッキ」・休憩の「強化」）
    DeckPanel m_deck;
    RestPanel m_rest;             // 休憩の窓
    MapPanel m_map;               // マップの窓
    BossRelicPanel m_bossRelic;   // ボスのレリックの3択
    // 窓のクラスに渡す描画と入力の道具
    UiContext Ui() { return { m_textRenderer, m_uiSprite, &m_uiInput, m_screenWidth, m_screenHeight, m_uiTime }; }
    bool AnyModalOpen() const;
    bool PanelPausesScene() const;        // シーンの Update を止める窓が開いているか
    // 入力（HandleInput は上から順に呼び、受け取った所で止まる）
    bool HandleConfirmAndTooltip();
    void HandleScrollDrag();
    void LogInputState();
    bool HandleOverlayInput(UiContext& ui);
    bool HandleTopBarInput(UiContext& ui);
    bool HandleFieldPanelInput(UiContext& ui);
    void PassInputToScene();
    bool TooltipBlocked() const;          // カードの説明を残せない画面が開いているか


    // 上部の帯の数値欄（0=HP, 1=Steps, 2=Layer, 3=G）。描画と説明のホバー判定で共有
    URect BarStatRect(int i) const
    {
        const float xs[] = { 8.0f, 136.0f, 268.0f, (float)m_screenWidth - 572.0f };
        const float ws[] = { 120.0f, 120.0f, 110.0f, 90.0f };
        return { xs[i], 6.0f, ws[i], 28.0f };
    }

    URect BarGearRect() const { return { (float)m_screenWidth - 58.0f, 5.0f, 48.0f, 30.0f }; }   // 帯の設定ボタン（ギア）
    // 上部の帯のボタン（0=マップ, 1=アイテム, 2=デッキ）。描画・クリック・説明で共有
    URect BarButtonRect(int i) const
    {
        const float xs[] = { (float)m_screenWidth - 680.0f, (float)m_screenWidth - 470.0f, (float)m_screenWidth - 220.0f };
        const float ws[] = { 36.0f, 90.0f, 100.0f };
        return { xs[i], 5.0f, ws[i], 30.0f };
    }

    // ホバー中のボタンは少し持ち上げて描く（判定と色は元の矩形のまま＝持ち上がっても外れない。UiContext と共通）
    URect Lifted(const URect& r) { return Ui().Lifted(r); }

    // 上部のバー（HP・デッキ・設定など）を出すシーンか
    bool HasTopBar() const { return m_currentType != SceneType::Title && m_currentType != SceneType::QuickSelect; }
};