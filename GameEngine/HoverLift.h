#pragma once
#include <unordered_map>
#include <cmath>

// ホバーで浮く動きを、手札のカードと同じ「目標へ指数的に近づく」補間で滑らかにする
// 状態は「浮かせる前の画面位置」をキーにここで持つので、呼ぶ側は状態を持たなくていい
namespace HoverLift
{
    constexpr float SPEED = 12.0f;   // 手札のカードの追従と同じ速さ

    struct State { float v = 0.0f; unsigned frame = 0; };
    inline std::unordered_map<long long, State> g_states;
    inline unsigned g_frame = 0;
    inline float    g_dt = 0.0f;

    // 毎フレーム1回（SceneManager::Update の先頭）
    inline void Tick(float dt) { g_dt = dt; g_frame++; }

    // 浮く量（0〜lift）を返す。x, y は浮かせる前の位置（＝キー）
    inline float Get(float x, float y, bool hovered, float lift)
    {
        long long key = ((long long)lroundf(x) << 32) ^ (long long)(unsigned)lroundf(y);
        State& s = g_states[key];
        if (s.frame == g_frame) return s.v;          // 同じフレームに2回目（文字の描画パスなど）は同じ値
        if (s.frame + 1 < g_frame) s.v = 0.0f;       // しばらく描かれていなかった（画面が変わった）ら沈んだ所から
        s.frame = g_frame;
        float k = g_dt * SPEED; if (k > 1.0f) k = 1.0f;
        s.v += ((hovered ? lift : 0.0f) - s.v) * k;
        return s.v;
    }
}
