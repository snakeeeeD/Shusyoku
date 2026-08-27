#pragma once
#include "Scene.h"
#include "SpriteRenderer.h"
#include "input.h"
#include "Scenetype.h"
#include "SceneManager.h"
#include <functional>
#include"Texturemanager.h"
#include "TextRenderer.h"

// SceneTypeだけ使えるように
enum class SceneType;

class TitleScene : public Scene
{
public:
    TitleScene();
    ~TitleScene();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    // シーン切り替え用コールバック
    std::function<void(SceneType)> onChangeScene;
    std::function<void(SceneType, std::function<void()>)> onChangeSceneBlack;

private:
    SpriteRenderer* m_spriteRenderer;
    TextRenderer* m_textRenderer;
    ID3D11ShaderResourceView* m_titleTexture;
    Input m_input;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;

    bool m_skipFirstFrame = true;

    bool m_dispHover = false;
    void DispBtnRect(float& x, float& y, float& w, float& h) const
    {
        w = 220.0f; h = 40.0f; x = 20.0f; y = (float)m_screenHeight - 60.0f;   // 左下
    }

    bool m_newHover = false;
    bool m_contHover = false;
    void MenuBtnRects(float& nx, float& ny, float& cx, float& cy, float& w, float& h) const
    {
        w = 280.0f; h = 60.0f;
        nx = (m_screenWidth - w) / 2.0f;
        ny = m_screenHeight / 2.0f + 40.0f;
        cx = nx; cy = ny + h + 18.0f;
    }
};