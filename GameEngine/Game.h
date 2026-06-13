#pragma once
#include"SpriteRenderer.h"
#include <d3d11.h>
#include "input.h"
#include "GridMap.h"
#include "SceneManager.h"

class Game
{
public:
    ~Game();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        	int screenWidth, int screenHeight, HWND hWnd,
            IDXGISwapChain* swapChain);
    void Update(float deltaTime);
    void Draw();
    void HandleInput();

private:
    SceneManager* m_sceneManager;
};
