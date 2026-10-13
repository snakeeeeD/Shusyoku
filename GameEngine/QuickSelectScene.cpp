#include "QuickSelectScene.h"
#include "QuickPlay.h"
#include "CardDataBase.h"
#include "CardVisual.h"
#include "CardTooltip.h"
#include "PlayerDataManager.h"
#include "RelicManager.h"
#include "ItemTooltip.h"
#include "UiWindow.h"
#include "TextureManager.h"
#include "Audio.h"
#include "GameUtils.h"
#include <algorithm>

// 左上(x,y)に scale 倍で置く時の CardVisual の基準座標（GetRect の逆）
static void ToBase(float x, float y, float s, float& bx, float& by)
{
    bx = x - CardVisual::CARD_W * (1.0f - s) / 2.0f;
    by = y - CardVisual::CARD_H * (1.0f - s) / 2.0f;
}

QuickSelectScene::~QuickSelectScene()
{
    delete m_sprite;
    delete m_text;
}

bool QuickSelectScene::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight, HWND hWnd, IDXGISwapChain* swapChain)
{
    m_screenWidth = screenWidth; m_screenHeight = screenHeight;
    m_sprite = new SpriteRenderer();
    if (!m_sprite->Init(device, context, screenWidth, screenHeight)) return false;
    m_text = new TextRenderer();
    if (!m_text->Init(device, context, swapChain)) return false;
    m_white = TextureManager::Get("white");
    m_input.SetWindowHandle(hWnd);
    return true;
}

void QuickSelectScene::Update(float deltaTime)
{
    m_input.Update();
    m_time += deltaTime;
}

void QuickSelectScene::CardSlot(int i, float& x, float& y) const
{
    URect g = GridRect();
    const float cw = CardVisual::CARD_W * CARD_SCALE, ch = CardVisual::CARD_H * CARD_SCALE;
    x = g.x + 20.0f + (i % GRID_COLS) * (cw + 8.0f);
    y = g.y + 50.0f + (i / GRID_COLS) * (ch + 12.0f);
}

void QuickSelectScene::Select(int i)
{
    m_selected = i;
    QuickPlay::BuildDeck(i);
    m_cards = PlayerDataManager::GetData().deck;
    std::sort(m_cards.begin(), m_cards.end(), [](const std::string& a, const std::string& b) {
        const CardData* da = CardDataBase::Get(a);
        const CardData* db = CardDataBase::Get(b);
        int ta = da ? (int)da->type : 99, tb = db ? (int)db->type : 99;
        return (ta != tb) ? ta < tb : a < b;   // 種類→IDの順（同じカードが隣に並ぶ）
        });
    m_hoverCard = -1;
}

void QuickSelectScene::HandleInput()
{
    // タイトルで押したクリックを持ち越さない（一度離すまで無視）
    if (!m_readyForInput)
    {
        if (!m_input.GetMouseButtonPress(0)) m_readyForInput = true;
        return;
    }

    POINT mp = m_input.GetMousePos();
    int n = (int)QuickPlay::Decks().size();

    // マウスが乗っているカード
    m_hoverCard = -1;
    const float cw = CardVisual::CARD_W * CARD_SCALE, ch = CardVisual::CARD_H * CARD_SCALE;
    for (int i = 0; i < (int)m_cards.size(); i++)
    {
        float x, y; CardSlot(i, x, y);
        if (mp.x >= x && mp.x <= x + cw && mp.y >= y && mp.y <= y + ch) { m_hoverCard = i; break; }
    }

    // ボタン（ホバー音は乗ったボタンが変わった瞬間だけ）
    const int KEY_BACK = 100, KEY_START = 101;
    int key = -1;
    for (int i = 0; i < n; i++) if (DeckBtn(i).has(mp)) key = i;
    if (BackBtn().has(mp)) key = KEY_BACK;
    if (m_selected >= 0 && StartBtn().has(mp)) key = KEY_START;
    if (key != m_hoverKey && key >= 0) Audio::PlaySE("Assets/Sound/se/hover.mp3");
    m_hoverKey = key;

    if (!m_input.GetMouseButtonTrigger(0) || key < 0) return;
    Audio::PlaySE("Assets/Sound/se/click.mp3");
    if (key < n)
    {
        if (key != m_selected) Select(key);
    }
    else if (key == KEY_BACK)
    {
        if (onChangeScene) onChangeScene(SceneType::Title);   // タイトルへ戻ると本編の状態に戻る（QuickPlay::End）
    }
    else if (key == KEY_START)
    {
        QuickPlay::Begin();
        if (onChangeScene) onChangeScene(SceneType::Battle);
    }
}

void QuickSelectScene::Draw()
{
    const auto& decks = QuickPlay::Decks();
    POINT mp = m_input.GetMousePos();
    const XMFLOAT4 gold(1.0f, 0.85f, 0.2f, 1.0f);
    URect info = InfoRect(), grid = GridRect(), back = BackBtn(), start = StartBtn();
    bool canStart = (m_selected >= 0);

    // ---- スプライト ----
    m_sprite->Begin();
    m_sprite->DrawSprite(TextureManager::Get("cardSelect_bg"), 0.0f, 0.0f,
        (float)m_screenWidth, (float)m_screenHeight, 0.0f, XMFLOAT4(0.6f, 0.6f, 0.6f, 1.0f));   // 少し暗くして文字を読みやすく
    m_sprite->DrawSprite(TextureManager::Get("ui_banner"),
        m_screenWidth / 2.0f - 240.0f, 18.0f, 480.0f, 96.0f, 0.0f, XMFLOAT4(1, 1, 1, 1));

    for (int i = 0; i < (int)decks.size(); i++)
    {
        URect b = DeckBtn(i);
        bool sel = (i == m_selected), hov = b.has(mp);
        UiWindow::Button(m_sprite, m_white, b.x, b.y, b.w, b.h,
            sel ? XMFLOAT4(0.6f, 0.45f, 0.2f, 1.0f)
            : hov ? XMFLOAT4(0.35f, 0.5f, 0.7f, 1.0f) : XMFLOAT4(0.25f, 0.28f, 0.4f, 1.0f));
        if (sel) UiWindow::Frame(m_sprite, m_white, b.x, b.y, b.w, b.h, gold);
    }

    UiWindow::Draw(m_sprite, m_white, info.x, info.y, info.w, info.h);
    UiWindow::Draw(m_sprite, m_white, grid.x, grid.y, grid.w, grid.h);

    for (int i = 0; i < (int)m_cards.size(); i++)
    {
        const CardData* d = CardDataBase::Get(m_cards[i]);
        if (!d) continue;
        float x, y, bx, by; CardSlot(i, x, y); ToBase(x, y, CARD_SCALE, bx, by);
        CardVisual::DrawBase(m_sprite, m_white, bx, by, CARD_SCALE, 0.0f,
            CardVisual::GetCardColor(d->type), d, m_time);
    }

    UiWindow::Button(m_sprite, m_white, back.x, back.y, back.w, back.h,
        back.has(mp) ? XMFLOAT4(0.5f, 0.35f, 0.35f, 1.0f) : XMFLOAT4(0.35f, 0.25f, 0.25f, 1.0f));
    UiWindow::Button(m_sprite, m_white, start.x, start.y, start.w, start.h,
        !canStart ? XMFLOAT4(0.2f, 0.2f, 0.2f, 1.0f)
        : start.has(mp) ? XMFLOAT4(0.75f, 0.55f, 0.2f, 1.0f) : XMFLOAT4(0.6f, 0.42f, 0.15f, 1.0f));
    m_sprite->End();

    // ---- 文字 ----
    m_text->Begin();
    m_text->DrawOutlinedTextCentered(L"デッキを選ぶ", m_screenWidth / 2.0f, 18.0f, 480.0f, 96.0f, 30.0f,
        D2D1::ColorF(1.0f, 0.95f, 0.8f), D2D1::ColorF(0.25f, 0.04f, 0.06f), 3.0f);

    for (int i = 0; i < (int)decks.size(); i++)
    {
        URect b = DeckBtn(i);
        m_text->DrawTextCentered(decks[i].name.c_str(), b.x + b.w / 2.0f, b.y, b.w, 26.0f,
            D2D1::ColorF(1, 1, 1), b.h);
    }

    // 説明（名前・どんなデッキか・レリック）
    if (m_selected >= 0)
    {
        const QuickDeck& q = decks[m_selected];
        m_text->DrawText(q.name.c_str(), info.x + 20.0f, info.y + 16.0f, 24.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
        m_text->DrawText(q.desc.c_str(), info.x + 20.0f, info.y + 54.0f, 16.0f, D2D1::ColorF(0.95f, 0.95f, 0.95f));
        m_text->DrawText(L"レリック（マウスを乗せると説明）", info.x + 20.0f, info.y + 120.0f, 16.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
        const auto& relics = PlayerDataManager::GetData().relics;
        for (int i = 0; i < (int)relics.size(); i++)
            if (auto r = RelicManager::Get(relics[i]))
            {
                URect row = RelicRow(i);
                m_text->DrawText(ToWString(r->name).c_str(), row.x + 10.0f, row.y, 15.0f,
                    row.has(mp) ? D2D1::ColorF(1.0f, 1.0f, 0.5f) : D2D1::ColorF(0.9f, 0.9f, 0.9f));
            }
    }
    else
        m_text->DrawText(L"上のボタンからデッキを選ぶ", info.x + 20.0f, info.y + 20.0f, 16.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f));

    // 一覧の見出し
    if (m_selected >= 0)
    {
        wchar_t buf[64];
        swprintf_s(buf, L"デッキ（%d枚）　カードにマウスを乗せると詳細", (int)m_cards.size());
        m_text->DrawText(buf, grid.x + 20.0f, grid.y + 16.0f, 18.0f, D2D1::ColorF(1.0f, 0.9f, 0.6f));
    }
    else
        m_text->DrawTextCentered(L"デッキを選ぶと中身がここに表示されます",
            grid.x + grid.w / 2.0f, grid.y, grid.w, 20.0f, D2D1::ColorF(0.8f, 0.8f, 0.8f), grid.h);

    for (int i = 0; i < (int)m_cards.size(); i++)
    {
        const CardData* d = CardDataBase::Get(m_cards[i]);
        if (!d) continue;
        float x, y, bx, by; CardSlot(i, x, y); ToBase(x, y, CARD_SCALE, bx, by);
        CardVisual::DrawTexts(m_text, d, nullptr, bx, by, CARD_SCALE, 0.0f);
    }

    m_text->DrawTextCentered(L"戻る", back.x + back.w / 2.0f, back.y, back.w, 22.0f, D2D1::ColorF(1, 1, 1), back.h);
    m_text->DrawTextCentered(L"このデッキで始める", start.x + start.w / 2.0f, start.y, start.w, 24.0f,
        canStart ? D2D1::ColorF(1, 1, 1) : D2D1::ColorF(0.5f, 0.5f, 0.5f), start.h);
    m_text->End();

    // ---- レリックの説明（ゲーム内のレリック説明と同じ窓） ----
    if (m_selected >= 0)
    {
        const auto& relics = PlayerDataManager::GetData().relics;
        for (int i = 0; i < (int)relics.size(); i++)
            if (RelicRow(i).has(mp))
            {
                ItemTooltip::Draw(m_sprite, m_text, m_white, relics[i],
                    info.x + info.w + 10.0f, RelicRow(i).y - 8.0f, m_screenWidth, m_screenHeight);   // 説明欄の右に出す
                break;
            }
    }

    // ---- ホバー中のカードを拡大＋キーワード説明（最前面） ----
    if (m_hoverCard >= 0)
        if (const CardData* d = CardDataBase::Get(m_cards[m_hoverCard]))
        {
            float x, y; CardSlot(m_hoverCard, x, y);
            float w = CardVisual::CARD_W * ZOOM_SCALE, h = CardVisual::CARD_H * ZOOM_SCALE;
            float tx = x + CardVisual::CARD_W * CARD_SCALE / 2.0f - w / 2.0f;   // 小さいカードの中心に合わせる
            float ty = y + CardVisual::CARD_H * CARD_SCALE / 2.0f - h / 2.0f;
            if (tx < 6.0f) tx = 6.0f;
            if (tx + w > m_screenWidth - 6.0f) tx = m_screenWidth - 6.0f - w;
            if (ty < 6.0f) ty = 6.0f;
            if (ty + h > m_screenHeight - 6.0f) ty = m_screenHeight - 6.0f - h;
            float bx, by; ToBase(tx, ty, ZOOM_SCALE, bx, by);

            m_sprite->Begin();
            CardVisual::DrawBase(m_sprite, m_white, bx, by, ZOOM_SCALE, 0.0f,
                CardVisual::GetCardColor(d->type), d, m_time);
            m_sprite->End();
            m_text->Begin();
            CardVisual::DrawTexts(m_text, d, nullptr, bx, by, ZOOM_SCALE, 0.0f);
            m_text->End();
            CardTooltip::Draw(m_sprite, m_text, m_white, d, tx + w / 2.0f, ty, w, h, m_screenWidth, m_screenHeight);
        }
}