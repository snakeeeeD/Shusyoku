#pragma once
#include <cmath>

// ‰æ–Ê—h‚êB‹­‚³‚ð—^‚¦‚é‚ÆŒ¸Š‚µ‚È‚ª‚ç—h‚ê‚é
class ScreenShake
{
public:
    static void Add(float power)
    {
        if (power > m_power) m_power = power;   // ‰ÁŽZ‚¾‚Æ˜A‘±”í’e‚Å–\‚ê‚é‚Ì‚Å‹­‚¢•û‚ðÌ—p
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

    // ƒ_ƒ[ƒW—Ê ¨ —h‚ê‚Ì‹­‚³
    static float PowerForDamage(int dmg)
    {
        float p = dmg / 20.0f;
        return p < 0.25f ? 0.25f : (p > 1.0f ? 1.0f : p);
    }

private:
    static constexpr float DURATION = 0.4f;
    static constexpr float AMPLITUDE = 0.35f;
    static inline float m_power = 0.0f;
    static inline float m_time = 0.0f;
};