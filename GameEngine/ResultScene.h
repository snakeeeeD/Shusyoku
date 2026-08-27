#pragma once
#include "Scene.h"
#include "SceneType.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "TextureManager.h"
#include "PlayerDataManager.h"
#include "input.h"
#include <functional>

class ResultScene : public Scene
{
public:
    ResultScene();
    ~ResultScene();
    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain) override;
    void Update(float deltaTime) override;
    void Draw() override;
    void HandleInput() override;

    std::function<void(SceneType)> onChangeScene;    std::function<void(SceneType, std::function<void()>)> onChangeSceneBlack;
    void SetCleared(bool c) { m_cleared = c; }

private:
    SpriteRenderer* m_spriteRenderer = nullptr;
    TextRenderer* m_textRenderer = nullptr;
    Input m_input;
    ID3D11ShaderResourceView* m_whiteTexture = nullptr;
    int m_screenWidth = 0, m_screenHeight = 0;
    HWND m_hWnd = nullptr;
    bool m_cleared = false;
    float m_timer = 0.0f;
};