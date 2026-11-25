#pragma once
// SceneManager の上に開く窓（用語集・設定・イベントなど）が使う、描画と入力の道具。
// 窓のクラスには SceneManager ごとではなく、これだけを渡す（窓が触れる範囲をはっきりさせる）
#include "TextRenderer.h"
#include "SpriteRenderer.h"
#include "input.h"
#include "URect.h"
#include "HoverLift.h"
#include "CardVisual.h"
#include "TextureManager.h"

struct UiContext
{
    TextRenderer*   text = nullptr;
    SpriteRenderer* sprite = nullptr;
    Input*          input = nullptr;
    int   screenW = 0, screenH = 0;
    float time = 0.0f;                        // UIの経過時間（カードのきらめきなど）

    static constexpr float BAR_H = 40.0f;     // 上の帯の高さ（窓はこの下に暗幕を敷く）

    // ホバー中のボタンは少し持ち上げて描く（判定と色は元の矩形のまま＝持ち上がっても外れない）
    static constexpr float HOVER_LIFT = 3.0f;
    URect Lifted(const URect& r) const
    {
        float up = HoverLift::Get(r.x, r.y, r.has(input->GetMousePos()), HOVER_LIFT);   // 手札と同じ補間で浮く
        return URect{ r.x, r.y - up, r.w, r.h };
    }

    // カード1枚（textPass=false で本体、true で文字。Begin/End は呼ぶ側で本体と文字に分ける）
    void DrawCard(const CardData* d, float x, float y, float scale, bool textPass) const
    {
        if (!d) return;
        if (!textPass)
            CardVisual::DrawBase(sprite, TextureManager::Get("white"), x, y, scale, 0.0f,
                CardVisual::GetCardColor(d->type), d, time);
        else
            CardVisual::DrawTexts(text, d, nullptr, x, y, scale, 0.0f, 1.0f);
    }
};
