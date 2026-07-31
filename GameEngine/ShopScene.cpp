#include "ShopScene.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "GameUtils.h"

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

    auto pickShuffled = [](std::vector<std::string> ids, int n, auto pushFn) {
        for (int i = (int)ids.size() - 1; i > 0; i--) std::swap(ids[i], ids[rand() % (i + 1)]);
        for (int i = 0; i < n && i < (int)ids.size(); i++) pushFn(ids[i]);
        };

    // カード
    std::vector<std::string> cardIds = {
        "ATK_strike", "SKL_defend", "MOV_move", "ATK_Spin Slash", "MOV_dash",
        "ATK_poison_blade", "POW_power_attack", "POW_buff_defense"
    };
    pickShuffled(cardIds, CARD_COUNT, [&](const std::string& id) {
        const CardData* d = CardDataBase::Get(id);
        if (d) m_items.push_back({ id, PriceFor(d), false, ShopKind::Card });
        });

    // コア
    std::vector<std::string> coreIds;
    for (auto& kv : MaterialDataBase::AllBases()) coreIds.push_back(kv.first);
    pickShuffled(coreIds, CORE_COUNT, [&](const std::string& id) {
        m_items.push_back({ id, CORE_PRICE, false, ShopKind::Core });
        });

    // 素材
    std::vector<std::string> matIds;
    for (auto& kv : MaterialDataBase::AllMaterials()) matIds.push_back(kv.first);
    pickShuffled(matIds, MAT_COUNT, [&](const std::string& id) {
        m_items.push_back({ id, MAT_PRICE, false, ShopKind::Material });
        });

    int disc = RelicManager::SumValue("shopDiscount");
    if (disc > 0)
        for (auto& it : m_items)
            it.price = it.price * (100 - disc) / 100;
}

void ShopScene::Update(float deltaTime) 
{
    m_time += deltaTime; 
    m_input.Update(); 
}

void ShopScene::GetSlotBase(int i, float& cardX, float& baseY) const
{
    const float cw = CardVisual::CARD_W * SHOP_SCALE;
    const float ch = CardVisual::CARD_H * SHOP_SCALE;

    int cardN = 0;
    for (auto& it : m_items) if (it.kind == ShopKind::Card) cardN++;
    int itemN = (int)m_items.size() - cardN;

    int row, col, rowCount;
    if (i < cardN) { row = 0; col = i;          rowCount = cardN; }   // 1行目：カード
    else { row = 1; col = i - cardN;  rowCount = itemN; }   // 2行目：アイテム

    float row0Y = 140.0f;
    baseY = (row == 0) ? row0Y : row0Y + ch + 46.0f;

    float totalW = rowCount * cw + (rowCount - 1) * 30.0f;
    float startX = (m_screenWidth - totalW) / 2.0f;
    cardX = startX + col * (cw + 30.0f);
}

void ShopScene::Draw()
{
    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(TextureManager::Get("cardSelect_bg"), 0, 0,
        (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(1, 1, 1, 1));

    for (int i = 0; i < (int)m_items.size(); i++)
    {
        float cardX, by; GetSlotBase(i, cardX, by);
        float drawY = (i == m_hoveredIndex) ? by - 20.0f : by;

        if (m_items[i].kind == ShopKind::Card)
        {
            const CardData* d = CardDataBase::Get(m_items[i].id);
            if (!d) continue;
            XMFLOAT4 col = m_items[i].bought
                ? XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f)
                : CardVisual::GetCardColor(d->type, false);
            CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, cardX, drawY, SHOP_SCALE, 0.0f, col, d, m_time);
        }
        else
        {
            float x, y, w, h; CardVisual::GetRect(cardX, drawY, SHOP_SCALE, x, y, w, h);
            XMFLOAT4 col = m_items[i].bought ? XMFLOAT4(0.15f, 0.15f, 0.15f, 1.0f)
                : (m_items[i].kind == ShopKind::Core) ? XMFLOAT4(0.42f, 0.32f, 0.22f, 1.0f)
                : XMFLOAT4(0.20f, 0.35f, 0.35f, 1.0f);
            m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h, 0.0f, col);
        }
    }

    float leaveW = 160.0f, leaveH = 44.0f;
    float leaveX = (m_screenWidth - leaveW) / 2.0f, leaveY = m_screenHeight - 90.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, leaveX, leaveY, leaveW, leaveH, 0.0f,
        XMFLOAT4(0.3f, 0.3f, 0.35f, 0.95f));
    m_spriteRenderer->End();

    m_textRenderer->Begin();
    m_textRenderer->DrawText(L"SHOP", m_screenWidth / 2.0f - 40.0f, 55.0f, 26.0f, D2D1::ColorF(D2D1::ColorF::White));

    // 見出し（拡大で上にはみ出す分 fw を引いて、カード上端の少し上に置く）
    const float fw = (CardVisual::CARD_H * SHOP_SCALE - CardVisual::CARD_H) / 2.0f;
    int cardN = 0; for (auto& it : m_items) if (it.kind == ShopKind::Card) cardN++;
    if (cardN > 0)
    {
        float cx, by; GetSlotBase(0, cx, by);
        m_textRenderer->DrawText(L"CARDS", m_screenWidth / 2.0f - 40.0f, by - fw - 24.0f, 16.0f, D2D1::ColorF(0.8f, 0.85f, 1.0f));
    }
    if (cardN < (int)m_items.size())
    {
        float cx, by; GetSlotBase(cardN, cx, by);
        m_textRenderer->DrawText(L"ITEMS", m_screenWidth / 2.0f - 40.0f, by - fw - 24.0f, 16.0f, D2D1::ColorF(1.0f, 0.85f, 0.6f));
    }

    for (int i = 0; i < (int)m_items.size(); i++)
    {
        float cardX, by; GetSlotBase(i, cardX, by);
        float drawY = (i == m_hoveredIndex) ? by - 20.0f : by;
        float x, y, w, h; CardVisual::GetRect(cardX, drawY, SHOP_SCALE, x, y, w, h);

        if (m_items[i].kind == ShopKind::Card)
        {
            const CardData* d = CardDataBase::Get(m_items[i].id);
            if (!d) continue;
            CardVisual::DrawTexts(m_textRenderer, d, nullptr, cardX, drawY, SHOP_SCALE, 0.0f, 1.0f);
        }
        else
        {
            std::wstring nm, desc; const wchar_t* tag; D2D1_COLOR_F tagCol;
            if (m_items[i].kind == ShopKind::Core)
            {
                const BaseDef* b = MaterialDataBase::GetBase(m_items[i].id);
                if (b) { nm = ToWString(b->name); desc = ToWString(b->desc); }
                tag = L"CORE"; tagCol = D2D1::ColorF(1.0f, 0.85f, 0.5f);
            }
            else
            {
                const MaterialDef* mm = MaterialDataBase::GetMaterial(m_items[i].id);
                if (mm) { nm = ToWString(mm->name); desc = ToWString(mm->desc); }
                tag = L"MATERIAL"; tagCol = D2D1::ColorF(0.6f, 0.9f, 0.9f);
            }
            m_textRenderer->DrawText(nm.c_str(), x + 6, y + 10, 17.0f, D2D1::ColorF(D2D1::ColorF::White));
            m_textRenderer->DrawText(tag, x + 6, y + 34, 12.0f, tagCol);
            m_textRenderer->DrawText(CardVisual::WrapText(desc, 9).c_str(), x + 6, y + 56, 12.0f, D2D1::ColorF(D2D1::ColorF::LightGray));
        }

        if (m_items[i].bought)
            m_textRenderer->DrawText(L"SOLD", x + 40, y + h - 32, 22.0f, D2D1::ColorF(D2D1::ColorF::Red));
        else
        {
            wchar_t priceText[32]; swprintf_s(priceText, L"%d G", m_items[i].price);
            m_textRenderer->DrawText(priceText, x + 6, y + h - 32, 20.0f, D2D1::ColorF(1.0f, 0.9f, 0.3f));
        }
    }
    m_textRenderer->DrawText(L"Leave", leaveX + 50, leaveY + 12, 20.0f, D2D1::ColorF(D2D1::ColorF::White));
    m_textRenderer->End();
}

void ShopScene::HandleInput()
{
    if (!m_readyForInput) { if (!m_input.GetMouseButtonPress(0)) m_readyForInput = true; return; }

    POINT mousePos = m_input.GetMousePos();
    m_hoveredIndex = -1;
    for (int i = 0; i < (int)m_items.size(); i++)
    {
        float cardX, by; GetSlotBase(i, cardX, by);
        float x, y, w, h; CardVisual::GetRect(cardX, by, SHOP_SCALE, x, y, w, h);
        if (mousePos.x >= x && mousePos.x <= x + w && mousePos.y >= y && mousePos.y <= y + h)
        {
            m_hoveredIndex = i; break;
        }
    }

    if (m_hoveredIndex >= 0 && m_input.GetMouseButtonTrigger(0))
    {
        ShopItem& it = m_items[m_hoveredIndex];
        if (!it.bought && PlayerDataManager::SpendGold(it.price))
        {
            if (it.kind == ShopKind::Card) PlayerDataManager::AddCard(it.id);
            else                           PlayerDataManager::AddMaterial(it.id, 1);
            it.bought = true;
            PlayerDataManager::Save();
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