#include "TitleScene.h"
#include "TextureLoader.h"
#include "UiWindow.h"
#include "Settings.h"
#include "Audio.h"

TitleScene::TitleScene()
    : m_spriteRenderer(nullptr)
    , m_titleTexture(nullptr)
    , m_textRenderer(nullptr)
{
}

TitleScene::~TitleScene()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
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

    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
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
    POINT mp = m_input.GetMousePos();
    float bx, by, bw, bh; DispBtnRect(bx, by, bw, bh);
    bool hov = (mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh);
    float dy = hov ? -4.0f : 0.0f;

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

    UiWindow::Button(m_spriteRenderer, TextureManager::Get("white"), bx, by + dy, bw, bh,
        hov ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));

    m_spriteRenderer->End();

    m_textRenderer->Begin();

    m_textRenderer->DrawText(L"左クリック：ニューゲーム",
        m_screenWidth / 2.0f - 150.0f,
        m_screenHeight / 2.0f + 50.0f,
        28.0f, D2D1::ColorF(D2D1::ColorF::Red));

    m_textRenderer->DrawText(L"右クリック：コンティニュー",
        m_screenWidth / 2.0f - 150.0f,
        m_screenHeight / 2.0f + 90.0f,
        28.0f, D2D1::ColorF(D2D1::ColorF::Black));

    const wchar_t* label = (Settings::Get().displayMode == DisplayMode::Borderless)
        ? L"表示: 全画面" : L"表示: ウィンドウ";
    m_textRenderer->DrawText(label, bx + 16.0f, by + dy + 9.0f, 20.0f, D2D1::ColorF(1, 1, 1));

    m_textRenderer->End();
}

void TitleScene::HandleInput()
{
    // 初回フレームはクリックを無視（前シーンのクリックを拾わないように）
    if (m_skipFirstFrame)
    {
        m_skipFirstFrame = false;
        return;
    }

    // ニューゲーム開始時
    // 表示モードのトグル
    POINT mp = m_input.GetMousePos();
    float bx, by, bw, bh; DispBtnRect(bx, by, bw, bh);
    bool hov = (mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh);
    if (hov && !m_dispHover) Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_dispHover = hov;

    // ニューゲーム開始判定
    if (m_input.GetMouseButtonTrigger(0))
    {
        if (hov)   // ボタン上：モード切替（ニューゲームにしない）
        {
            DisplayMode next = (Settings::Get().displayMode == DisplayMode::Borderless)
                ? DisplayMode::Windowed : DisplayMode::Borderless;
            SetDisplayMode(next);
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            return;
        }
        PlayerDataManager::StartNewGame();
        if (onChangeScene)
            onChangeScene(SceneType::Field);
    }

    // コンティニュー
    if (m_input.GetMouseButtonTrigger(1))
    {
        if (!PlayerDataManager::HasSaveData()) return;
        PlayerDataManager::Load();
        if (onChangeScene)
            onChangeScene(SceneType::Field);
    }
}