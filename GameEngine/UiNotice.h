#pragma once

enum class NoticeType { None, HandFull };

// 画面中央に短く出す通知（表示文はUI側が決める）
class UiNotice
{
public:
    static void Show(NoticeType type, float duration = 1.5f)
    {
        m_type = type;
        m_timer = duration;
        m_triggered = true;
    }

    // 発生した瞬間を1回だけ受け取る（UI側の演出開始用）
    static bool ConsumeTriggered()
    {
        bool t = m_triggered;
        m_triggered = false;
        return t;
    }

    static void Update(float deltaTime)
    {
        if (m_timer <= 0.0f) return;
        m_timer -= deltaTime;
        if (m_timer <= 0.0f) m_type = NoticeType::None;
    }

    static bool  IsActive() { return m_timer > 0.0f; }
    static NoticeType GetType() { return m_type; }
    static float GetAlpha() { return (m_timer > 0.4f) ? 1.0f : m_timer / 0.4f; }
    static float GetRise() { return (1.5f - m_timer) * 20.0f; }   // ふわっと上へ
    static void Clear() { m_timer = 0.0f; m_type = NoticeType::None; m_triggered = false; }

private:
    static inline NoticeType m_type = NoticeType::None;
    static inline float m_timer = 0.0f;
    static inline bool  m_triggered = false;
};