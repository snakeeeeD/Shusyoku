#pragma once

enum class TurnBannerType { None, Player, Enemy, BattleStart };

class TurnBanner
{
public:
    static void Show(TurnBannerType type, float duration = 1.1f)
    {
        m_type = type; m_timer = duration; m_total = duration; m_next = TurnBannerType::None;
    }
    static void ShowThen(TurnBannerType first, TurnBannerType then, float duration = 1.1f)
    {
        Show(first, duration); m_next = then;      // first‚ÌŒã‚Éthen‚ðŽ©“®•\Ž¦
    }
    static void Update(float dt)
    {
        if (m_timer <= 0.0f) return;
        m_timer -= dt;
        if (m_timer <= 0.0f)
        {
            m_timer = 0.0f;
            if (m_next != TurnBannerType::None) Show(m_next);   // —\–ñ‚³‚ê‚½ŽŸ‚Ö
        }
    }
    static bool IsActive() { return m_timer > 0.0f && m_type != TurnBannerType::None; }
    static TurnBannerType GetType() { return m_type; }
    static float GetAlpha()
    {
        float e = m_total - m_timer; const float f = 0.25f;
        if (e < f) return e / f;
        if (m_timer < f) return m_timer / f;
        return 1.0f;
    }
    static float GetSlideX()
    {
        float e = m_total - m_timer; const float f = 0.25f;
        if (e < f) return (e / f - 1.0f) * 60.0f;
        return 0.0f;
    }
    static void Clear() { m_timer = 0.0f; m_type = TurnBannerType::None; m_next = TurnBannerType::None; }
private:
    static inline TurnBannerType m_type = TurnBannerType::None;
    static inline TurnBannerType m_next = TurnBannerType::None;
    static inline float m_timer = 0.0f;
    static inline float m_total = 1.1f;
};