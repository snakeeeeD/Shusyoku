#pragma once
#include <algorithm>

// 縦にスクロールする一覧：ホイールで動かし、なめらかに追いつき、見せる範囲の端で薄くなる
struct ScrollList
{
    float pos = 0.0f, target = 0.0f;   // 今のずれ／目標のずれ
    float top = 0.0f, bottom = 0.0f;   // 見せる範囲（画面のy）
    float contentH = 0.0f;             // 一覧全体の高さ（最後の段の下に FADE 分の余白を含める）
    static constexpr float FADE = 36.0f;   // 端で薄くなる幅

    void  Reset() { pos = target = 0.0f; }
    float Max() const { float m = contentH - (bottom - top); return m > 0.0f ? m : 0.0f; }
    void  Wheel(int delta) { target -= delta * 0.5f; }
    void  Update(float dt)
    {
        target = (std::clamp)(target, 0.0f, Max());
        pos += (target - pos) * (std::min)(1.0f, 15.0f * dt);
    }
    // y の位置の濃さ（範囲の外は0。端の FADE 分で薄くなる。全部収まる時は薄くしない）
    float AlphaAt(float y) const
    {
        if (Max() <= 0.0f) return 1.0f;
        if (y < top - FADE || y > bottom) return 0.0f;
        float a = 1.0f;
        if (y < top) a = (y - (top - FADE)) / FADE;
        if (bottom - y < FADE) a = (std::min)(a, (bottom - y) / FADE);
        return (std::clamp)(a, 0.0f, 1.0f);
    }
    // 四角（カードなど）の濃さ：上の端と下の端の薄いほう
    float AlphaOfRect(float y, float h) const { return (std::min)(AlphaAt(y), AlphaAt(y + h)); }
};
