#include "ResultScene.h"
#include "QuickPlay.h"
#include "UiWindow.h"
#include "Audio.h"

ResultScene::ResultScene() {}
ResultScene::~ResultScene() { delete m_spriteRenderer; delete m_textRenderer; }

bool ResultScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth; m_screenHeight = screenHeight; m_hWnd = hWnd;
    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight)) return false;
    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain)) return false;
    m_whiteTexture = TextureManager::Get("white");
    m_input.SetWindowHandle(hWnd);
    return true;
}

void ResultScene::Update(float deltaTime)
{
    m_input.Update();
    m_timer += deltaTime;
}

void ResultScene::Draw()
{
    auto& pd = PlayerDataManager::GetData();

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
        m_cleared ? XMFLOAT4(0.05f, 0.08f, 0.05f, 1.0f) : XMFLOAT4(0.08f, 0.03f, 0.03f, 1.0f));

    bool quick = QuickPlay::Active();
    POINT mp = m_input.GetMousePos();
    if (quick)
    {
        URect a = MainBtn(), b = TitleBtn();
        UiWindow::Button(m_spriteRenderer, m_whiteTexture, a.x, a.y, a.w, a.h,
            a.has(mp) ? XMFLOAT4(0.75f, 0.55f, 0.2f, 1.0f) : XMFLOAT4(0.6f, 0.42f, 0.15f, 1.0f));
        UiWindow::Button(m_spriteRenderer, m_whiteTexture, b.x, b.y, b.w, b.h,
            b.has(mp) ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));
    }

    m_spriteRenderer->End();

    m_textRenderer->Begin();
    if (!quick)
    {
        if (m_cleared)
            m_textRenderer->DrawText(L"RUN CLEAR!", m_screenWidth / 2.0f - 160.0f, m_screenHeight / 2.0f - 120.0f, 60.0f, D2D1::ColorF(D2D1::ColorF::Gold));
        else
            m_textRenderer->DrawText(L"GAME OVER", m_screenWidth / 2.0f - 150.0f, m_screenHeight / 2.0f - 120.0f, 60.0f, D2D1::ColorF(D2D1::ColorF::Red));

        wchar_t buf[64];
        swprintf_s(buf, L"Gold: %d", pd.gold);
        m_textRenderer->DrawText(buf, m_screenWidth / 2.0f - 60.0f, m_screenHeight / 2.0f - 20.0f, 24.0f, D2D1::ColorF(D2D1::ColorF::White));
        swprintf_s(buf, L"Deck: %d", (int)pd.deck.size());
        m_textRenderer->DrawText(buf, m_screenWidth / 2.0f - 60.0f, m_screenHeight / 2.0f + 14.0f, 24.0f, D2D1::ColorF(D2D1::ColorF::White));

        m_textRenderer->DrawText(L"click to title", m_screenWidth / 2.0f - 70.0f, m_screenHeight / 2.0f + 90.0f, 22.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f));
    }
    else
    {
        float cx = m_screenWidth / 2.0f;
        m_textRenderer->DrawTextCentered(m_cleared ? L"クイックプレイ クリア！" : L"GAME OVER",
            cx, m_screenHeight / 2.0f - 150.0f, 900.0f, 52.0f,
            m_cleared ? D2D1::ColorF(D2D1::ColorF::Gold) : D2D1::ColorF(D2D1::ColorF::Red));
        m_textRenderer->DrawTextCentered(
            L"本編では マップを進んでカードを集め、合成で自分だけのカードも作れる",
            cx, m_screenHeight / 2.0f - 50.0f, 1000.0f, 20.0f, D2D1::ColorF(0.9f, 0.9f, 0.9f));
        URect a = MainBtn(), b = TitleBtn();
        m_textRenderer->DrawTextCentered(L"本編を始める", a.x + a.w / 2.0f, a.y, a.w, 24.0f, D2D1::ColorF(1, 1, 1), a.h);
        m_textRenderer->DrawTextCentered(L"タイトルへ", b.x + b.w / 2.0f, b.y, b.w, 24.0f, D2D1::ColorF(1, 1, 1), b.h);
    }
    m_textRenderer->End();
}

void ResultScene::HandleInput()
{
    if (m_timer < 0.6f) return;                  // 0.6秒間はクリックを無視（持ち越しクリック対策）
    if (!m_input.GetMouseButtonTrigger(0) || !onChangeSceneBlack) return;
    if (QuickPlay::Active())
    {
        POINT mp = m_input.GetMousePos();
        if (MainBtn().has(mp))   // 本編を始める：クイックを終えて（本編の状態に戻して）から新規ラン
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            onChangeSceneBlack(SceneType::Field, []() { QuickPlay::End(); PlayerDataManager::StartNewGame(); });
        }
        else if (TitleBtn().has(mp))   // タイトルへ（切替時に QuickPlay::End が呼ばれる）
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            onChangeSceneBlack(SceneType::Title, []() {});
        }
        return;
    }
    onChangeSceneBlack(SceneType::Title, []() { PlayerDataManager::StartNewGame(); });
}