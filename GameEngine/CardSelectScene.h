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
    static constexpr float CARD_W = 150.0f;
    static constexpr float CARD_H = 200.0f;
    static constexpr float SEL_SCALE = 1.4f;

    RewardMode m_mode = RewardMode::Normal;

    bool m_skipHover = false;
};