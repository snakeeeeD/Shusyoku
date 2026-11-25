#pragma once
#include"SpriteRenderer.h"
#include <d3d11.h>
#include "input.h"
#include "GridMap.h"
#include "SceneManager.h"
#include "Telemetry.h" 

class Game
{
public:
    ~Game();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        	int screenWidth, int screenHeight, HWND hWnd,
            IDXGISwapChain* swapChain);
    void Update(float deltaTime);
    void Draw();
    void DrawImGui();
    void HandleInput();

private:
    SceneManager* m_sceneManager = nullptr;

    bool m_prevF8 = false;      // F8エッジ検出用
    int  m_feedbackCount = 0;   // フィードバック通し番号
};
