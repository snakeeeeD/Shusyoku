#pragma once
#include <string>
#include <functional>
#include "SpriteRenderer.h"
#include "TextRenderer.h"
#include "UiWindow.h"
#include "CardVisual.h"
#include "CardDataBase.h"
#include "URect.h"
#include "Audio.h"

enum class ConfirmKind { Upgrade, Remove };

// 強化・削除の確認ウィンドウ。どの画面からでも Open で開き、SceneManager が一番手前に描いて操作を受ける
class CardConfirm
{
public:
    static void Open(ConfirmKind kind, const std::string& cardId, std::function<void()> onYes)
    {
        s_active = true; s_kind = kind; s_id = cardId; s_onYes = std::move(onYes);
    }
    static bool Active() { return s_active; }
    static void Close() { s_active = false; s_onYes = nullptr; }

    static void HandleInput(POINT m, bool click, int sw, int sh)
    {
        if (!s_active || !click) return;
        URect panel, yes, no; Layout(sw, sh, panel, yes, no);
        if (yes.has(m))
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            auto f = s_onYes;
            Close();
            if (f) f();
        }
        else if (no.has(m) || !panel.has(m))
        {
            Audio::PlaySE("Assets/Sound/se/click.mp3");
            Close();
        }
    }

    static void Draw(SpriteRenderer* sr, TextRenderer* tr, ID3D11ShaderResourceView* white, POINT m, int sw, int sh)
    {
        if (!s_active) return;
        URect panel, yes, no; Layout(sw, sh, panel, yes, no);
        const CardData* before = CardDataBase::Get(s_id);
        const CardData* after = (s_kind == ConfirmKind::Upgrade) ? CardDataBase::Get(s_id + "+") : nullptr;

        // カードの位置（強化は 強化前 ▶ 強化後 の2枚、削除は1枚）
        const float cw = CardVisual::CARD_W * SCALE, ch = CardVisual::CARD_H * SCALE;
        float cx = panel.x + panel.w / 2.0f, cy = panel.y + 70.0f;
        float x1 = after ? cx - cw - GAP / 2.0f : cx - cw / 2.0f;
        float x2 = cx + GAP / 2.0f;
        auto base = [&](float x, float& bx, float& by) {   // 左上 → DrawBase の基準位置
            bx = x + (cw - CardVisual::CARD_W) / 2.0f;
            by = cy + (ch - CardVisual::CARD_H) / 2.0f;
            };

        sr->Begin();
        sr->DrawSprite(white, 0, 0, (float)sw, (float)sh, 0.0f, DirectX::XMFLOAT4(0, 0, 0, 0.65f));   // 暗幕
        UiWindow::Draw(sr, white, panel.x, panel.y, panel.w, panel.h);
        float bx, by;
        if (before) { base(x1, bx, by); CardVisual::DrawBase(sr, white, bx, by, SCALE, 0.0f, CardVisual::GetCardColor(before->type), before, 0.0f); }
        if (after)  { base(x2, bx, by); CardVisual::DrawBase(sr, white, bx, by, SCALE, 0.0f, CardVisual::GetCardColor(after->type), after, 0.0f); }
        bool removing = (s_kind == ConfirmKind::Remove);
        UiWindow::Button(sr, white, yes.x, yes.y, yes.w, yes.h,
            removing ? (yes.has(m) ? DirectX::XMFLOAT4(0.75f, 0.30f, 0.30f, 1) : DirectX::XMFLOAT4(0.50f, 0.24f, 0.24f, 1))
                     : (yes.has(m) ? DirectX::XMFLOAT4(0.35f, 0.6f, 0.35f, 1) : DirectX::XMFLOAT4(0.25f, 0.45f, 0.25f, 1)));
        UiWindow::Button(sr, white, no.x, no.y, no.w, no.h,
            no.has(m) ? DirectX::XMFLOAT4(0.35f, 0.5f, 0.7f, 1) : DirectX::XMFLOAT4(0.25f, 0.28f, 0.4f, 1));
        sr->End();

        tr->Begin();
        const D2D1_COLOR_F W = D2D1::ColorF(1, 1, 1), GOLD = D2D1::ColorF(1.0f, 0.85f, 0.4f);
        tr->DrawTextCentered(removing ? L"このカードを削除しますか？" : L"このカードを強化しますか？",
            cx, panel.y + 14.0f, panel.w, 24.0f, D2D1::ColorF(1, 0.9f, 0.6f), 40.0f);
        if (before) { base(x1, bx, by); CardVisual::DrawTexts(tr, before, nullptr, bx, by, SCALE, 0.0f, 1.0f); }
        if (after)
        {
            base(x2, bx, by); CardVisual::DrawTexts(tr, after, nullptr, bx, by, SCALE, 0.0f, 1.0f);
            tr->DrawTextCentered(L"▶", cx, cy + ch / 2.0f - 24.0f, GAP, 34.0f, GOLD, 48.0f);
            tr->DrawTextCentered(L"強化前", x1 + cw / 2.0f, cy + ch + 6.0f, cw, 15.0f, D2D1::ColorF(0.75f, 0.75f, 0.8f));
            tr->DrawTextCentered(L"強化後", x2 + cw / 2.0f, cy + ch + 6.0f, cw, 15.0f, GOLD);
        }
        tr->DrawTextCentered(removing ? L"削除する" : L"強化する", yes.x + yes.w / 2.0f, yes.y, yes.w, 20.0f, W, yes.h);
        tr->DrawTextCentered(L"やめる", no.x + no.w / 2.0f, no.y, no.w, 20.0f, W, no.h);
        tr->End();
    }

private:
    static constexpr float SCALE = 1.4f;   // カードの大きさ
    static constexpr float GAP = 90.0f;    // 2枚の間（▶ を置く）

    static void Layout(int sw, int sh, URect& panel, URect& yes, URect& no)
    {
        float pw = 640.0f, ph = 70.0f + CardVisual::CARD_H * SCALE + 100.0f;
        float px = (sw - pw) / 2.0f, py = (sh - ph) / 2.0f;
        panel = { px, py, pw, ph };
        yes = { px + pw / 2.0f - 180.0f, py + ph - 60.0f, 170.0f, 42.0f };
        no = { px + pw / 2.0f + 10.0f, py + ph - 60.0f, 170.0f, 42.0f };
    }

    static inline bool s_active = false;
    static inline ConfirmKind s_kind = ConfirmKind::Upgrade;
    static inline std::string s_id;
    static inline std::function<void()> s_onYes;
};
