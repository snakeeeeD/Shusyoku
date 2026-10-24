#include "TitleScene.h"
#include "TextureLoader.h"
#include "UiWindow.h"
#include "Settings.h"
#include "Audio.h"
#include "QuickPlay.h"

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

    float nx, ny, cx, cy, mw, mh; MenuBtnRects(nx, ny, cx, cy, mw, mh);
    bool hasSave = PlayerDataManager::HasSaveData() && !PlayerDataManager::IsRunOver();
    bool nHov = (mp.x >= nx && mp.x <= nx + mw && mp.y >= ny && mp.y <= ny + mh);
    bool cHov = hasSave && (mp.x >= cx && mp.x <= cx + mw && mp.y >= cy && mp.y <= cy + mh);
    float nDy = nHov ? -4.0f : 0.0f, cDy = cHov ? -4.0f : 0.0f;
    float qx, qy, qw, qh; QuickBtnRect(qx, qy, qw, qh);
    bool qHov = (mp.x >= qx && mp.x <= qx + qw && mp.y >= qy && mp.y <= qy + qh);
    float qDy = qHov ? -4.0f : 0.0f;

    m_spriteRenderer->Begin();
    if (m_titleTexture)
        m_spriteRenderer->DrawSprite(m_titleTexture, 0.0f, 0.0f,
            (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(1, 1, 1, 1));

    // 表示モード切替ボタン（既存・左下）
    UiWindow::Button(m_spriteRenderer, TextureManager::Get("white"), bx, by + dy, bw, bh,
        hov ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));

    // ニューゲーム（常時）
    UiWindow::Button(m_spriteRenderer, TextureManager::Get("white"), nx, ny + nDy, mw, mh,
        nHov ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));
    // コンティニュー（セーブがある時だけ）
    if (hasSave)
        UiWindow::Button(m_spriteRenderer, TextureManager::Get("white"), cx, cy + cDy, mw, mh,
            cHov ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));
    // クイックプレイ（5分体験）は色を変えて目立たせる
    UiWindow::Button(m_spriteRenderer, TextureManager::Get("white"), qx, qy + qDy, qw, qh,
        qHov ? XMFLOAT4(0.6f, 0.45f, 0.2f, 1.0f) : XMFLOAT4(0.42f, 0.3f, 0.15f, 1.0f));
    m_spriteRenderer->End();

    m_textRenderer->Begin();
    m_textRenderer->DrawTextCentered(L"ニューゲーム", nx + mw / 2.0f, ny + nDy, mw, 28.0f, D2D1::ColorF(1, 1, 1), mh);
    if (hasSave)
        m_textRenderer->DrawTextCentered(L"コンティニュー", cx + mw / 2.0f, cy + cDy, mw, 28.0f, D2D1::ColorF(1, 1, 1), mh);
    m_textRenderer->DrawTextCentered(L"クイックプレイ（5分）", qx + qw / 2.0f, qy + qDy, qw, 26.0f, D2D1::ColorF(1, 1, 1), qh);
    m_textRenderer->DrawTextCentered(L"設定", bx + bw / 2.0f, by + dy, bw, 20.0f, D2D1::ColorF(1, 1, 1), bh);
    m_textRenderer->End();
}

void TitleScene::HandleInput()
{
    if (m_skipFirstFrame) { m_skipFirstFrame = false; return; }

    POINT mp = m_input.GetMousePos();
    float bx, by, bw, bh; DispBtnRect(bx, by, bw, bh);
    bool hov = (mp.x >= bx && mp.x <= bx + bw && mp.y >= by && mp.y <= by + bh);
    if (hov && !m_dispHover) Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_dispHover = hov;

    float qx, qy, qw, qh; QuickBtnRect(qx, qy, qw, qh);
    bool qHov = (mp.x >= qx && mp.x <= qx + qw && mp.y >= qy && mp.y <= qy + qh);
    if (qHov && !m_quickHover) Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_quickHover = qHov;

    float nx, ny, cx, cy, mw, mh; MenuBtnRects(nx, ny, cx, cy, mw, mh);
    bool hasSave = PlayerDataManager::HasSaveData() && !PlayerDataManager::IsRunOver();
    bool nHov = (mp.x >= nx && mp.x <= nx + mw && mp.y >= ny && mp.y <= ny + mh);
    bool cHov = hasSave && (mp.x >= cx && mp.x <= cx + mw && mp.y >= cy && mp.y <= cy + mh);
    if (nHov && !m_newHover)  Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_newHover = nHov;
    if (cHov && !m_contHover) Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_contHover = cHov;

    if (m_input.GetMouseButtonTrigger(0))
    {
        if (hov)   // 設定を開く（表示モードの切り替えも設定の中にある）
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            if (onOpenSettings) onOpenSettings();
            return;
        }
        if (nHov)   // ニューゲーム
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            if (onChangeSceneBlack)
                onChangeSceneBlack(SceneType::Field, []() { PlayerDataManager::StartNewGame(); });
            return;
        }
        if (cHov)   // コンティニュー
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            if (onChangeSceneBlack)
                onChangeSceneBlack(SceneType::Field, []() { PlayerDataManager::Load(); });
            return;
        }
        if (qHov)   // クイックプレイ（仮：Step Bで選択画面に差し替える）
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            if (onChangeSceneBlack)
                onChangeSceneBlack(SceneType::QuickSelect, []() { QuickPlay::Enter(); });
            return;
        }
    }
}