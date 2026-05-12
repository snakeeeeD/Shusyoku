#include "Game.h"

bool Game::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_sceneManager = new SceneManager();
    return m_sceneManager->Init(device, context, screenWidth, screenHeight, hWnd, swapChain);
}

void Game::Update(float deltaTime)
{
    m_sceneManager->Update(deltaTime);
}

void Game::Draw()
{
    m_sceneManager->Draw();
}

void Game::HandleInput()
{
    m_sceneManager->HandleInput();
}