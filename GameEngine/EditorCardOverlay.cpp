#include "EditorCardOverlay.h"

#ifdef _DEBUG

#include "CardVisual.h"
#include "External/imgui/imgui.h"
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "TextureManager.h"

namespace
{
    // ゲーム画面（論理 1280x720）
    constexpr float SCREEN_W = 1280.0f, SCREEN_H = 720.0f;
    constexpr float SCALE = 1.8f;
    constexpr float GAP = 40.0f;
}

void EditorCardOverlay::Draw(SpriteRenderer* sprite, TextRenderer* text, const Item* items, int count, bool left)
{
    if (!sprite || !text || count <= 0) return;

    ID3D11ShaderResourceView* white = TextureManager::Get("white");
    float cw = CardVisual::CARD_W * SCALE, ch = CardVisual::CARD_H * SCALE;
    float totalW = cw * count + GAP * (count - 1);
    float x0 = left ? 30.0f : SCREEN_W - totalW - 30.0f;
    float y0 = SCREEN_H / 2.0f - ch / 2.0f + 20.0f;

    // CardVisual::GetRect は「基準位置から中心で拡大」なので、左上が (left, y0) になる基準位置に直す
    auto baseX = [&](float l) { return l + (cw - CardVisual::CARD_W) / 2.0f; };
    float by = y0 + (ch - CardVisual::CARD_H) / 2.0f;
    float t = (float)ImGui::GetTime();

    sprite->Begin();
    sprite->DrawSprite(white, x0 - 16.0f, y0 - 50.0f, totalW + 32.0f, ch + 66.0f, 0.0f, XMFLOAT4(0, 0, 0, 0.55f));
    for (int i = 0; i < count; i++)
    {
        if (!items[i].card) continue;
        float l = x0 + (cw + GAP) * i;
        CardVisual::DrawBase(sprite, white, baseX(l), by, SCALE, 0.0f,
            CardVisual::GetCardColor(items[i].card->type), items[i].card, t);
    }
    sprite->End();

    text->Begin();
    for (int i = 0; i < count; i++)
    {
        float l = x0 + (cw + GAP) * i;
        if (items[i].card)
            CardVisual::DrawTexts(text, items[i].card, nullptr, baseX(l), by, SCALE, 0.0f, 1.0f);
        if (items[i].label)
            text->DrawText(items[i].label, l, y0 - 40.0f, 20.0f,
                items[i].highlight ? D2D1::ColorF(0.4f, 1.0f, 0.5f) : D2D1::ColorF(1, 1, 1));
    }
    text->End();
}

#endif
