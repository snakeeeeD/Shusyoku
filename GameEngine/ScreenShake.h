#pragma once
#include <cmath>
#include "Settings.h"


// 画面揺れ。強さを与えると減衰しながら揺れる
class ScreenShake
{
public:
    static void Add(float power)
    {
        if (!Settings::Get().screenShake) return;   // 設定でOFFなら無視
        if (power > m_power) m_power = power;   // 加算だと連続被弾で暴れるので強い方を採用
        if (m_power > 1.0f) m_power = 1.0f;
    }

    static void Update(float deltaTime)
    {
        m_time += deltaTime;
        if (m_power <= 0.0f) return;
        m_power -= deltaTime / DURATION;
        if (m_power < 0.0f) m_power = 0.0f;
    }

    static void GetOffset(float& outX, float& outZ)
    {
        outX = sinf(m_time * 32.0f) * m_power * AMPLITUDE;
        outZ = cosf(m_time * 25.0f) * m_power * AMPLITUDE;
    }

    static void Clear() { m_power = 0.0f; }

    // ダメージ量 → 揺れの強さ
    static float PowerForDamage(int dmg)
    {
        float p = dmg / 12.0f;                              // 分母を小さく＝ダメージあたり強く
        return p < 0.3f ? 0.3f : (p > 2.5f ? 2.5f : p);     // 上限2.5＝大ダメージほど大きく揺れる
    }

private:
    static constexpr float DURATION = 0.4f;
    static constexpr float AMPLITUDE = 0.35f;
    static inline float m_power = 0.0f;
    static inline float m_time = 0.0f;
};