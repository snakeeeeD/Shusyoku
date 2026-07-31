#include "ShopScene.h"
#include "MaterialDataBase.h"
#include "RelicManager.h"
#include "GameUtils.h"

#include <cstdlib>
#include <algorithm>

ShopScene::ShopScene() {}
ShopScene::~ShopScene() { delete m_spriteRenderer; delete m_textRenderer; }

static int SellPriceOf(const std::string& id)
{
    if (auto b = MaterialDataBase::GetBase(id)) return b->sellPrice;
    if (auto m = MaterialDataBase::GetMaterial(id)) return m->sellPrice;
    return 0;
}

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
        std::vector<std::string> coreIds;
    for (auto& kv : MaterialDataBase::AllBases())
        if (kv.second.buyPrice > 0) coreIds.push_back(kv.first);
    pickShuffled(coreIds, CORE_COUNT, [&](const std::string& id) {
        m_items.push_back({ id, MaterialDataBase::GetBase(id)->buyPrice, false, ShopKind::Core });
        });

    std::vector<std::string> matIds;
    for (auto& kv : MaterialDataBase::AllMaterials())
        if (kv.second.buyPrice > 0) matIds.push_back(kv.first);   // 買値0は店に出さない
    pickShuffled(matIds, MAT_COUNT, [&](const std::string& id) {
        m_items.push_back({ id, MaterialDataBase::GetMaterial(id)->buyPrice, false, ShopKind::Material });
        });

    // カード
    std::vector<std::string> cardIds = {
        "ATK_strike", "SKL_defend", "MOV_move", "ATK_Spin Slash", "MOV_dash",
        "ATK_poison_blade", "POW_power_attack", "POW_buff_defense"
    };
    pickShuffled(cardIds, CARD_COUNT, [&](const std::string& id) {
        const CardData* d = CardDataBase::Get(id);
        if (d) m_items.push_back({ id, PriceFor(d), false, ShopKind::Card });
        });


    // レリック
    auto relicPool = RelicManager::ShopPool();
    pickShuffled(relicPool, RELIC_STOCK, [&](const std::string& id) {
        m_items.push_back({ id, RELIC_PRICE, false, ShopKind::Relic });
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
                : (m_items[i].kind == ShopKind::Relic) ? XMFLOAT4(0.40f, 0.30f, 0.55f, 1.0f)
                : XMFLOAT4(0.20f, 0.35f, 0.35f, 1.0f);
            m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, w, h, 0.0f, col);
        }
    }

    float rmX = 40.0f, rmY = m_screenHeight - 70.0f, rmW = 220.0f, rmH = 44.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, rmX, rmY, rmW, rmH, 0.0f,
        m_removedThisShop ? XMFLOAT4(0.25f, 0.25f, 0.25f, 0.95f) : XMFLOAT4(0.5f, 0.25f, 0.25f, 0.95f));

    float slX = 280.0f, slY = m_screenHeight - 70.0f, slW = 160.0f, slH = 44.0f;
    m_spriteRenderer->DrawSprite(m_whiteTexture, slX, slY, slW, slH, 0.0f, XMFLOAT4(0.25f, 0.42f, 0.25f, 0.95f));

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
            else if (m_items[i].kind == ShopKind::Material)
            {
                const MaterialDef* mm = MaterialDataBase::GetMaterial(m_items[i].id);
                if (mm) { nm = ToWString(mm->name); desc = ToWString(mm->desc); }
                tag = L"MATERIAL"; tagCol = D2D1::ColorF(0.6f, 0.9f, 0.9f);
            }
            else   // Relic
            {
                const RelicDef* r = RelicManager::Get(m_items[i].id);
                if (r) { nm = ToWString(r->name); desc = ToWString(r->desc); }
                tag = L"RELIC"; tagCol = D2D1::ColorF(0.85f, 0.7f, 1.0f);
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

    if (m_removedThisShop)
        m_textRenderer->DrawText(L"削除済み", 95.0f, m_screenHeight - 58.0f, 20.0f, D2D1::ColorF(0.6f, 0.6f, 0.6f));
    else
    {
        wchar_t rmT[48]; swprintf_s(rmT, L"カード削除 %dG", RemovePrice());
        m_textRenderer->DrawText(rmT, 60.0f, m_screenHeight - 58.0f, 20.0f, D2D1::ColorF(1, 1, 1));
    }

    m_textRenderer->DrawText(L"売却", slX + 55.0f, slY + 12.0f, 20.0f, D2D1::ColorF(1, 1, 1));

    m_textRenderer->DrawText(L"Leave", leaveX + 50, leaveY + 12, 20.0f, D2D1::ColorF(D2D1::ColorF::White));
    m_textRenderer->End();

    if (m_removeMode) DrawDeckRemoval();
    if (m_sellMode) DrawSellMode();
}

void ShopScene::HandleInput()
{
    if (!m_readyForInput) { if (!m_input.GetMouseButtonPress(0)) m_readyForInput = true; return; }

    POINT mousePos = m_input.GetMousePos();

    // 削除モード中
    if (m_removeMode)
    {
        if (m_input.GetMouseButtonTrigger(0))
        {
            int idx = DeckCardAt(mousePos);
            if (idx >= 0 && PlayerDataManager::SpendGold(RemovePrice()))
            {
                PlayerDataManager::RemoveCard(idx);
                PlayerDataManager::GetData().removeCount++;   // 次回から値上げ
                PlayerDataManager::Save();
                m_removedThisShop = true;                     // このショップは終了
            }
            m_removeMode = false;
        }
        return;
    }
    // カード削除ボタン
    {
        float rmX = 40.0f, rmY = m_screenHeight - 70.0f, rmW = 220.0f, rmH = 44.0f;
        if (!m_removedThisShop && m_input.GetMouseButtonTrigger(0)
            && mousePos.x >= rmX && mousePos.x <= rmX + rmW && mousePos.y >= rmY && mousePos.y <= rmY + rmH)
        {
            if (PlayerDataManager::GetData().gold >= RemovePrice()) m_removeMode = true;
            return;
        }
    }
    // 売却モード
    if (m_sellMode)
    {
        if (m_input.GetMouseButtonTrigger(0))
        {
            int idx = SellItemAt(mousePos);
            if (idx >= 0)
            {
                const std::string id = OwnedItemIds()[idx];
                PlayerDataManager::GetData().gold += SellPriceOf(id);
                PlayerDataManager::AddMaterial(id, -1);
                PlayerDataManager::Save();
            }
            else m_sellMode = false;   // 余白で戻る
        }
        return;
    }
    // 売却ボタン
    {
        float slX = 280.0f, slY = m_screenHeight - 70.0f, slW = 160.0f, slH = 44.0f;
        if (m_input.GetMouseButtonTrigger(0)
            && mousePos.x >= slX && mousePos.x <= slX + slW && mousePos.y >= slY && mousePos.y <= slY + slH)
        {
            m_sellMode = true; return;
        }
    }

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
            else if (it.kind == ShopKind::Relic) PlayerDataManager::AddRelic(it.id);
            else PlayerDataManager::AddMaterial(it.id, 1);
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

void ShopScene::GetDeckSlot(int i, float& x, float& y) const
{
    const float s = 0.9f;
    float cw = CardVisual::CARD_W * s, ch = CardVisual::CARD_H * s;
    int perRow = 8; float gapX = 12.0f, gapY = 16.0f;
    float totalW = perRow * cw + (perRow - 1) * gapX;
    float startX = (m_screenWidth - totalW) / 2.0f;
    x = startX + (i % perRow) * (cw + gapX);
    y = 130.0f + (i / perRow) * (ch + gapY);
}

int ShopScene::DeckCardAt(POINT p) const
{
    auto& deck = PlayerDataManager::GetData().deck;
    for (int i = 0; i < (int)deck.size(); i++)
    {
        float bx, by; GetDeckSlot(i, bx, by);
        float x, y, w, h; CardVisual::GetRect(bx, by, 0.9f, x, y, w, h);
        if (p.x >= x && p.x <= x + w && p.y >= y && p.y <= y + h) return i;
    }
    return -1;
}

void ShopScene::DrawDeckRemoval()
{
    auto& deck = PlayerDataManager::GetData().deck;
    int hov = DeckCardAt(m_input.GetMousePos());

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f,
        XMFLOAT4(0.0f, 0.0f, 0.05f, 0.92f));
    for (int i = 0; i < (int)deck.size(); i++)
    {
        const CardData* d = CardDataBase::Get(deck[i]);
        if (!d) continue;
        float bx, by; GetDeckSlot(i, bx, by);
        if (i == hov) by -= 12.0f;
        CardVisual::DrawBase(m_spriteRenderer, m_whiteTexture, bx, by, 0.9f, 0.0f,
            i == hov ? XMFLOAT4(0.7f, 0.2f, 0.2f, 1.0f) : CardVisual::GetCardColor(d->type), d, m_time);
    }
    m_spriteRenderer->End();

    m_textRenderer->Begin();
    wchar_t t[64]; swprintf_s(t, L"削除するカードを選択 (%dG)", RemovePrice());
    m_textRenderer->DrawText(t, m_screenWidth / 2.0f - 150.0f, 80.0f, 26.0f, D2D1::ColorF(1.0f, 0.6f, 0.6f));
    for (int i = 0; i < (int)deck.size(); i++)
    {
        const CardData* d = CardDataBase::Get(deck[i]);
        if (!d) continue;
        float bx, by; GetDeckSlot(i, bx, by);
        if (i == hov) by -= 12.0f;
        CardVisual::DrawTexts(m_textRenderer, d, nullptr, bx, by, 0.9f, 0.0f, 1.0f);
    }
    m_textRenderer->DrawText(L"カードをクリックで削除 / 余白で戻る", m_screenWidth / 2.0f - 170.0f, m_screenHeight - 55.0f, 20.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f));
    m_textRenderer->End();
}

int ShopScene::RemovePrice() const
{
    return REMOVE_BASE + PlayerDataManager::GetData().removeCount * REMOVE_STEP;
}

std::vector<std::string> ShopScene::OwnedItemIds() const
{
    std::vector<std::string> v;
    for (auto& kv : PlayerDataManager::GetData().materials)
        if (kv.second > 0) v.push_back(kv.first);
    return v;
}
void ShopScene::GetSellSlot(int i, float& x, float& y) const
{
    float w = 160.0f, h = 40.0f, gap = 10.0f; int cols = 6;
    float startX = (m_screenWidth - (cols * w + (cols - 1) * gap)) / 2.0f;
    x = startX + (i % cols) * (w + gap);
    y = 140.0f + (i / cols) * (h + gap);
}
int ShopScene::SellItemAt(POINT p) const
{
    auto ids = OwnedItemIds();
    for (int i = 0; i < (int)ids.size(); i++)
    {
        float x, y; GetSellSlot(i, x, y);
        if (p.x >= x && p.x <= x + 160.0f && p.y >= y && p.y <= y + 40.0f) return i;
    }
    return -1;
}

void ShopScene::DrawSellMode()
{
    auto ids = OwnedItemIds();
    auto& mats = PlayerDataManager::GetData().materials;
    int hov = SellItemAt(m_input.GetMousePos());

    m_spriteRenderer->Begin();
    m_spriteRenderer->DrawSprite(m_whiteTexture, 0, 0, (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0.0f, 0.0f, 0.05f, 0.92f));
    for (int i = 0; i < (int)ids.size(); i++)
    {
        float x, y; GetSellSlot(i, x, y);
        bool isCore = MaterialDataBase::GetBase(ids[i]) != nullptr;
        XMFLOAT4 col = (i == hov) ? XMFLOAT4(0.55f, 0.5f, 0.2f, 1.0f)
            : isCore ? XMFLOAT4(0.42f, 0.32f, 0.22f, 1.0f) : XMFLOAT4(0.2f, 0.35f, 0.35f, 1.0f);
        m_spriteRenderer->DrawSprite(m_whiteTexture, x, y, 160.0f, 40.0f, 0.0f, col);
    }
    m_spriteRenderer->End();

    m_textRenderer->Begin();
    m_textRenderer->DrawText(L"売却するアイテムを選択", m_screenWidth / 2.0f - 150.0f, 90.0f, 26.0f, D2D1::ColorF(1.0f, 0.9f, 0.4f));
    for (int i = 0; i < (int)ids.size(); i++)
    {
        float x, y; GetSellSlot(i, x, y);
        std::wstring nm;
        if (auto b = MaterialDataBase::GetBase(ids[i])) nm = ToWString(b->name);
        else if (auto m = MaterialDataBase::GetMaterial(ids[i])) nm = ToWString(m->name);
        wchar_t buf[96];
        swprintf_s(buf, L"%s x%d  (%dG)", nm.c_str(), mats[ids[i]], SellPriceOf(ids[i]));
        m_textRenderer->DrawText(buf, x + 6.0f, y + 10.0f, 14.0f, D2D1::ColorF(1, 1, 1));
    }
    m_textRenderer->DrawText(L"クリックで1個売却 / 余白で戻る", m_screenWidth / 2.0f - 150.0f, m_screenHeight - 55.0f, 20.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f));
    m_textRenderer->End();
}