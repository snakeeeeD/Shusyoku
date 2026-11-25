#include "Game.h"

Game::~Game()
{
    Telemetry::Instance().Shutdown();
    delete m_sceneManager;
}

bool Game::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_sceneManager = new SceneManager();
    bool ok = m_sceneManager->Init(device, context, screenWidth, screenHeight, hWnd, swapChain);
    Telemetry::Instance().Init("v0.1");
    return ok;
}

void Game::Update(float deltaTime)
{
    m_sceneManager->Update(deltaTime);

    // F8: その場フィードバックマーカー（今の層/階/シーン付きで記録）
    bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (f8 && !m_prevF8)
        Telemetry::Instance().Log("feedback", { {"marker", ++m_feedbackCount} });
    m_prevF8 = f8;
}

void Game::Draw()
{
    m_sceneManager->Draw();
}

void Game::DrawImGui()
{
    m_sceneManager->DrawImGui();
}

void Game::HandleInput()
{
    m_sceneManager->HandleInput();
}