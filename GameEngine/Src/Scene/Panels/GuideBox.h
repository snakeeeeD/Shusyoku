#pragma once
// 説明の窓（その場のヒント・カメラデモの説明で共通）。状態を持たない描画の道具
#include "UiContext.h"

namespace GuideBox
{
    // 画面上寄りの中央、左の金帯が目印。icon は文の左、note は右下に小さく
    void Draw(UiContext& ui, const wchar_t* text, float alpha, const char* icon, const wchar_t* note);
    // 今のヒント（Hint.h）：指している場所の金枠の脈動＋説明の窓
    void DrawHint(UiContext& ui);
}
