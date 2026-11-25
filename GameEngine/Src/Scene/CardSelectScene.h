#pragma once
#include "Scene.h"
#include "SceneManager.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "TextureManager.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "PlayerDataManager.h"
#include "input.h"
#include <vector>
#include <string>
#include <functional>

class CardSelectScene : public Scene
{
public:
    CardSelectScene();
    ~CardSelectScene();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd,
        IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    std::function<void(SceneType)> onChangeScene;

    enum class RewardMode { Normal, Rare, Upgraded };
    void SetMode(RewardMode m) { m_mode = m; }

private:
    void GenerateChoices();

    SpriteRenderer* m_spriteRenderer;
    TextRenderer* m_textRenderer;
    Input           m_input;

    ID3D11ShaderResourceView* m_whiteTexture;

    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;

    std::vector<std::string> m_choices; // 選択肢のカードID
    int m_hoveredIndex;
    float m_time = 0.0f;

    bool m_readyForInput = false;

    static constexpr int   CHOICE_COUNT = 3;
    static constexpr float SEL_GAP = 30.0f;    // カードの間
    static constexpr float HOVER_UP = 20.0f;   // マウスを乗せたカードが浮く量
    // i 枚目のカードを CardVisual に渡す基準（1倍のカードの左上）。拡大した並びが画面の真ん中に来るように直した位置
    void CardBase(int i, float& bx, float& by) const;
    static constexpr float SEL_SCALE = 2.0f;   // 報酬のカードの大きさ（1倍＝100x140）。手札より大きくして、説明と札を読みやすく

    RewardMode m_mode = RewardMode::Normal;

    bool m_skipHover = false;
};