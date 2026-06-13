#include "CardSelectScene.h"
#include <cstdlib>

CardSelectScene::CardSelectScene()
    : m_spriteRenderer(nullptr)
    , m_textRenderer(nullptr)
    , m_whiteTexture(nullptr)
    , m_screenWidth(0)
    , m_screenHeight(0)
    , m_hWnd(nullptr)
    , m_hoveredIndex(-1)
{
}

CardSelectScene::~CardSelectScene()
{
    delete m_spriteRenderer;
    delete m_textRenderer;
}

bool CardSelectScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd,
    IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    m_hWnd = hWnd;

    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight))
        return false;

    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain))
        return false;

    m_whiteTexture = TextureManager::Get("white");

    m_input.SetWindowHandle(hWnd);

    GenerateChoices();

    return true;
}

void CardSelectScene::GenerateChoices()
{
    m_choices.clear();

    // 全カードIDを取得してランダムに3枚選ぶ
    // 今はハードコードで仮実装
    std::vector<std::string> allCards = {
        "strike", "defend", "move", "Spin Slash", "dash"
    };

    // シャッフルして先頭3枚を選ぶ
    for (int i = (int)allCards.size() - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        std::swap(allCards[i], allCards[j]);
    }

    for (int i = 0; i < CHOICE_COUNT && i < (int)allCards.size(); i++)
        m_choices.push_back(allCards[i]);
}

void CardSelectScene::Update(float deltaTime)
{
    m_input.Update();
}

void CardSelectScene::Draw()
{
    const float totalW = CHOICE_COUNT * CARD_W + (CHOICE_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - CARD_H) / 2.0f;

    m_spriteRenderer->Begin();

    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        const CardData* data = CardDataBase::Get(m_choices[i]);
        if (!data) continue;

        float cardX = startX + i * (CARD_W + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        XMFLOAT4 color = CardVisual::GetCardColor(
            data->type,
            false
        );

        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, drawY,
            CARD_W, CARD_H, 0.0f, color);
    }

    m_spriteRenderer->End();

    m_textRenderer->Begin();

    // タイトル
    m_textRenderer->DrawText(L"カードを1枚選んでください",
        m_screenWidth / 2.0f - 150.0f, 80.0f, 28.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    // カード情報
    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        const CardData* data = CardDataBase::Get(m_choices[i]);
        if (!data) continue;

        float cardX = startX + i * (CARD_W + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        m_textRenderer->DrawText(data->name.c_str(),
            cardX + 5.0f, drawY + 10.0f, 18.0f,
            D2D1::ColorF(D2D1::ColorF::White));

        wchar_t costText[32];
        swprintf_s(costText, L"Cost: %d", data->cost);
        m_textRenderer->DrawText(costText,
            cardX + 5.0f, drawY + 36.0f, 14.0f,
            D2D1::ColorF(D2D1::ColorF::Yellow));

        m_textRenderer->DrawText(
            GetCardEffectText(data).c_str(),
            cardX + 5.0f, drawY + 60.0f, 13.0f,
            D2D1::ColorF(D2D1::ColorF::LightGray));
    }

    m_textRenderer->End();
}

void CardSelectScene::HandleInput()
{
    POINT mousePos = m_input.GetMousePos();

    const float totalW = CHOICE_COUNT * CARD_W + (CHOICE_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - CARD_H) / 2.0f;

    // ホバー判定
    m_hoveredIndex = -1;
    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        float cardX = startX + i * (CARD_W + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        bool hover = mousePos.x >= cardX && mousePos.x <= cardX + CARD_W
            && mousePos.y >= drawY && mousePos.y <= drawY + CARD_H;

        if (hover)
        {
            m_hoveredIndex = i;
            break;
        }
    }

    // クリックでカード獲得
    if (m_hoveredIndex >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        PlayerDataManager::AddCard(m_choices[m_hoveredIndex]);
        OutputDebugStringW(L"★ カード獲得！\n");

        // バトルシーンに戻る（後でフィールドに変更）
        if (onChangeScene)
            onChangeScene(SceneType::Field);
    }
}

std::wstring CardSelectScene::GetCardEffectText(const CardData* data) const
{
    if (!data) return L"";

    std::wstring result = data->description;

    std::wstring placeholder = L"{value}";
    size_t pos = result.find(placeholder);

    if (pos != std::wstring::npos)
    {
        result.replace(
            pos,
            placeholder.size(),
            std::to_wstring(data->mainEffect.value)
        );
    }

    return result;
}