#include "CardSelectScene.h"
#include "RelicManager.h"

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
    m_readyForInput = false;
    m_choices.clear();

    bool rare = PlayerDataManager::GetData().rewardRare || RelicManager::HasKind("rewardRare");
    PlayerDataManager::GetData().rewardRare = false;

    // 全カードを重み付きで集める
    struct WeightedCard {
        std::string id;
        int weight;
    };
    std::vector<WeightedCard> pool;

    std::vector<std::string> allIds = {
        "ATK_strike", "SKL_defend", "MOV_move", "ATK_Spin Slash", "MOV_dash",
        "ATK_poison_blade", "POW_power_attack", "POW_buff_defense"
    };

    for (const auto& id : allIds)
    {
        const CardData* data = CardDataBase::Get(id);
        if (!data) continue;

        int w;
        if (rare)
            w = (data->rarity == CardRarity::Rare) ? 12
            : (data->rarity == CardRarity::Uncommon) ? 6 : 1;
        else
            w = (data->rarity == CardRarity::Rare) ? 2
            : (data->rarity == CardRarity::Uncommon) ? 5 : 10;
        pool.push_back({ id, w });
    }

    // 重み付きランダムで3枚選ぶ（重複なし）
    for (int i = 0; i < CHOICE_COUNT && !pool.empty(); i++)
    {
        int totalWeight = 0;
        for (const auto& p : pool) totalWeight += p.weight;

        int roll = rand() % totalWeight;
        int cumulative = 0;
        int picked = 0;

        for (int j = 0; j < (int)pool.size(); j++)
        {
            cumulative += pool[j].weight;
            if (roll < cumulative) { picked = j; break; }
        }

        m_choices.push_back(pool[picked].id);
        pool.erase(pool.begin() + picked);
    }
}

void CardSelectScene::Update(float deltaTime)
{
    m_time += deltaTime;
    m_input.Update();
}

void CardSelectScene::Draw()
{
    const float cw = CardVisual::CARD_W * SEL_SCALE;
    const float ch = CardVisual::CARD_H * SEL_SCALE;
    const float totalW = CHOICE_COUNT * cw + (CHOICE_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - ch) / 2.0f;

    m_spriteRenderer->Begin();

    m_spriteRenderer->DrawSprite(
        TextureManager::Get("cardSelect_bg"),
        0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight,
        0.0f, XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f));

    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        const CardData* data = CardDataBase::Get(m_choices[i]);
        if (!data) continue;

        float cardX = startX + i * (cw + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, cardX, drawY, SEL_SCALE, 0.0f,
            CardVisual::GetCardColor(data->type), data, m_time);
    }

    // スキップボタン
    float skipW = 140.0f, skipH = 44.0f;
    float skipX = (m_screenWidth - skipW) / 2.0f;
    float skipY = m_screenHeight - 120.0f;
    POINT mp = m_input.GetMousePos();
    bool skipHover = mp.x >= skipX && mp.x <= skipX + skipW
        && mp.y >= skipY && mp.y <= skipY + skipH;
    m_spriteRenderer->DrawSprite(m_whiteTexture, skipX, skipY, skipW, skipH, 0.0f,
        skipHover ? XMFLOAT4(0.5f, 0.3f, 0.3f, 1.0f) : XMFLOAT4(0.3f, 0.2f, 0.2f, 0.9f));

    m_spriteRenderer->End();

    m_textRenderer->Begin();

    // タイトル
    m_textRenderer->DrawText(L"カードを1枚選んでください",
        m_screenWidth / 2.0f - 150.0f, 80.0f, 28.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    m_textRenderer->DrawText(L"スキップ", skipX + 30.0f, skipY + 12.0f, 20.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    // カード情報
    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        const CardData* data = CardDataBase::Get(m_choices[i]);
        if (!data) continue;

        float cardX = startX + i * (cw + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        CardVisual::DrawTexts(m_textRenderer, data, nullptr, cardX, drawY, SEL_SCALE, 0.0f, 1.0f);
    }

    m_textRenderer->End();
}

void CardSelectScene::HandleInput()
{
    // シーン移行時のクリック持ち越しを無視（一度離すまで）
    if (!m_readyForInput)
    {
        if (!m_input.GetMouseButtonPress(0))
            m_readyForInput = true;
        return;
    }

    POINT mousePos = m_input.GetMousePos();

    const float cw = CardVisual::CARD_W * SEL_SCALE;
    const float ch = CardVisual::CARD_H * SEL_SCALE;
    const float totalW = CHOICE_COUNT * cw + (CHOICE_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - ch) / 2.0f;

    m_hoveredIndex = -1;
    for (int i = 0; i < (int)m_choices.size(); i++)
    {
        float cardX = startX + i * (cw + 30.0f);
        float x, y, w, h; CardVisual::GetRect(cardX, cardY, SEL_SCALE, x, y, w, h);
        if (mousePos.x >= x && mousePos.x <= x + w
            && mousePos.y >= y && mousePos.y <= y + h)
        {
            m_hoveredIndex = i; break;
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

    float skipW = 140.0f, skipH = 44.0f;
    float skipX = (m_screenWidth - skipW) / 2.0f;
    float skipY = m_screenHeight - 120.0f;
    bool skipHover = mousePos.x >= skipX && mousePos.x <= skipX + skipW
        && mousePos.y >= skipY && mousePos.y <= skipY + skipH;
    if (skipHover && m_input.GetMouseButtonTrigger(0))
    {
        if (onChangeScene) onChangeScene(SceneType::Field);
        return;
    }
}