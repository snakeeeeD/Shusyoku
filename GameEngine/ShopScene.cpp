#include "ShopScene.h"
#include <cstdlib>
#include <algorithm>

ShopScene::ShopScene() {}
ShopScene::~ShopScene() { delete m_spriteRenderer; delete m_textRenderer; }

bool ShopScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth; m_screenHeight = screenHeight; m_hWnd = hWnd;
    m_spriteRenderer = new SpriteRenderer();
    if (!m_spriteRenderer->Init(device, context, screenWidth, screenHeight)) return false;
    m_textRenderer = new TextRenderer();
    if (!m_textRenderer->Init(device, context, swapChain)) return false;
    m_whiteTexture = TextureManager::Get("white");
    m_input.SetWindowHandle(hWnd);
    GenerateStock();
    return true;
}

int ShopScene::PriceFor(const CardData* data) const
{
    if (!data) return 50;
    if (data->rarity == CardRarity::Rare)     return 150;
    if (data->rarity == CardRarity::Uncommon) return 75;
    return 50;
}

void ShopScene::GenerateStock()
{
    m_readyForInput = false;
    m_items.clear();
    std::vector<std::string> allIds = {
        "ATK_strike", "SKL_defend", "MOV_move", "ATK_Spin Slash", "MOV_dash",
        "ATK_poison_blade", "POW_power_attack", "POW_buff_defense"
    };
    for (int i = (int)allIds.size() - 1; i > 0; i--) std::swap(allIds[i], allIds[rand() % (i + 1)]);
    for (int i = 0; i < STOCK_COUNT && i < (int)allIds.size(); i++)
    {
        const CardData* d = CardDataBase::Get(allIds[i]);
        if (!d) continue;
        m_items.push_back({ allIds[i], PriceFor(d), false });
    }
}

void ShopScene::Update(float) { m_input.Update(); }

void ShopScene::Draw()
{
    const float totalW = STOCK_COUNT * CARD_W + (STOCK_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - CARD_H) / 2.0f;

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(TextureManager::Get("cardSelect_bg"), 0, 0,
        (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(1, 1, 1, 1));

    for (int i = 0; i < (int)m_items.size(); i++)
    {
        const CardData* d = CardDataBase::Get(m_items[i].id);
        if (!d) continue;
        float cardX = startX + i * (CARD_W + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;
        XMFLOAT4 color = m_items[i].bought
            ? XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f)
            : CardVisual::GetCardColor(d->type, false);
        m_spriteRenderer->DrawSprite(m_whiteTexture, cardX, drawY, CARD_W, CARD_H, 0.0f, color);
    }

    float leaveW = 160.0f, leaveH = 44.0f;
    float leaveX = (m_screenWidth - leaveW) / 2.0f, leaveY = m_screenHeight - 90.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, leaveX, leaveY, leaveW, leaveH, 0.0f,
        XMFLOAT4(0.3f, 0.3f, 0.35f, 0.95f));
    m_spriteRenderer->End();

    m_textRenderer->Begin();
    m_textRenderer->DrawText(L"SHOP", m_screenWidth / 2.0f - 40.0f, 60.0f, 28.0f,
        D2D1::ColorF(D2D1::ColorF::White));

    for (int i = 0; i < (int)m_items.size(); i++)
    {
        const CardData* d = CardDataBase::Get(m_items[i].id);
        if (!d) continue;
        float cardX = startX + i * (CARD_W + 30.0f);
        float drawY = (i == m_hoveredIndex) ? cardY - 20.0f : cardY;

        m_textRenderer->DrawText(d->name.c_str(), cardX + 5, drawY + 10, 18.0f, D2D1::ColorF(D2D1::ColorF::White));
        wchar_t costText[32]; swprintf_s(costText, L"Cost: %d", d->cost);
        m_textRenderer->DrawText(costText, cardX + 5, drawY + 36, 14.0f, D2D1::ColorF(D2D1::ColorF::Yellow));
        m_textRenderer->DrawText(GetCardEffectText(d).c_str(), cardX + 5, drawY + 60, 13.0f, D2D1::ColorF(D2D1::ColorF::LightGray));

        if (m_items[i].bought)
            m_textRenderer->DrawText(L"SOLD", cardX + 45, drawY + CARD_H - 34, 22.0f, D2D1::ColorF(D2D1::ColorF::Red));
        else {
            wchar_t priceText[32]; swprintf_s(priceText, L"%d G", m_items[i].price);
            m_textRenderer->DrawText(priceText, cardX + 5, drawY + CARD_H - 34, 22.0f, D2D1::ColorF(1.0f, 0.9f, 0.3f));
        }
    }
    m_textRenderer->DrawText(L"Leave", leaveX + 50, leaveY + 12, 20.0f, D2D1::ColorF(D2D1::ColorF::White));
    m_textRenderer->End();
}

void ShopScene::HandleInput()
{
    if (!m_readyForInput) { if (!m_input.GetMouseButtonPress(0)) m_readyForInput = true; return; }

    POINT mousePos = m_input.GetMousePos();
    const float totalW = STOCK_COUNT * CARD_W + (STOCK_COUNT - 1) * 30.0f;
    const float startX = (m_screenWidth - totalW) / 2.0f;
    const float cardY = (m_screenHeight - CARD_H) / 2.0f;

    m_hoveredIndex = -1;
    for (int i = 0; i < (int)m_items.size(); i++)
    {
        float cardX = startX + i * (CARD_W + 30.0f);
        if (mousePos.x >= cardX && mousePos.x <= cardX + CARD_W
            && mousePos.y >= cardY && mousePos.y <= cardY + CARD_H) {
            m_hoveredIndex = i; break;
        }
    }

    if (m_hoveredIndex >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        ShopItem& it = m_items[m_hoveredIndex];
        if (!it.bought && PlayerDataManager::SpendGold(it.price))
        {
            PlayerDataManager::AddCard(it.id);
            it.bought = true;
        }
    }

    float leaveW = 160.0f, leaveH = 44.0f;
    float leaveX = (m_screenWidth - leaveW) / 2.0f, leaveY = m_screenHeight - 90.0f;
    if (mousePos.x >= leaveX && mousePos.x <= leaveX + leaveW
        && mousePos.y >= leaveY && mousePos.y <= leaveY + leaveH
        && m_input.GetMouseButtonTrigger(0))
    {
        if (onChangeScene) onChangeScene(SceneType::Field);
    }
}

std::wstring ShopScene::GetCardEffectText(const CardData* data) const
{
    if (!data) return L"";
    std::wstring result = data->description;
    std::wstring ph = L"{value}";
    size_t pos = result.find(ph);
    if (pos != std::wstring::npos)
        result.replace(pos, ph.size(), std::to_wstring(data->mainEffect.value));
    return result;
}