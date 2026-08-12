#pragma once

// ヒットストップ：命中時に一瞬だけゲームを止めて手応えを出す
class HitStop
{
public:
    static void Add(float duration)
    {
        if (duration > m_timer) m_timer = duration;   // 長い方を採用（連続ヒットで伸びすぎない）
    }
    static void Update(float dt)
    {
        if (m_timer > 0.0f) m_timer -= dt;
    }
    static bool IsActive() { return m_timer > 0.0f; }
    static void Clear() { m_timer = 0.0f; }
private:
    static inline float m_timer = 0.0f;
};