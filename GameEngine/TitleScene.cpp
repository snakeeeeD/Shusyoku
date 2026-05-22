#include "TitleScene.h"
#include "TextureLoader.h"

TitleScene::TitleScene()
    : m_spriteRenderer(nullptr)
    , m_titleTexture(nullptr)
{

}

TitleScene::~TitleScene()
{
    delete m_spriteRenderer;
    if (m_titleTexture) m_titleTexture->Release();
}

bool TitleScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    // スプライトレンダラー
    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, m_screenWidth, screenHeight))
        return false;

    
    m_titleTexture = TextureManager::Get("title");

    m_input.SetWindowHandle(hWnd);

    return true;
}

void TitleScene::Update(float deltaTime)
{
    m_input.Update();
}

void TitleScene::Draw()
{
    m_spriteRenderer->Begin();



    // タイトル画像（画面中央に表示）
    if (m_titleTexture)
    {
        float imgW = m_screenWidth;
        float imgH = m_screenHeight;
        float x = (m_screenWidth - imgW) / 2.0f;
        float y = (m_screenHeight - imgH) / 2.0f;
        m_spriteRenderer->DrawSprite(m_titleTexture, x, y, imgW, imgH,
            0.0f, XMFLOAT4(1, 1, 1, 1));
    }

    m_spriteRenderer->End();
}

void TitleScene::HandleInput()
{
    // ニューゲーム開始時
    if (m_input.GetMouseButtonTrigger(0))
    {
        PlayerDataManager::StartNewGame();
        if (onChangeScene)
            onChangeScene(SceneType::Field);
    }

    // コンティニュー
    if (m_input.GetMouseButtonTrigger(1))
    {
        PlayerDataManager::Load();
        if (onChangeScene)
            onChangeScene(SceneType::Field);
    }
}