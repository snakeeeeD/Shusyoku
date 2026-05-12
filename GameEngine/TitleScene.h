#pragma once
#include "Scene.h"
#include "SpriteRenderer.h"
#include "input.h"
#include "Scenetype.h"
#include "SceneManager.h"
#include <functional>
#include"Texturemanager.h"

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

private:
    SpriteRenderer* m_spriteRenderer;
    ID3D11ShaderResourceView* m_titleTexture;
    Input m_input;

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;
    int m_screenWidth;
    int m_screenHeight;
    HWND m_hWnd;
};