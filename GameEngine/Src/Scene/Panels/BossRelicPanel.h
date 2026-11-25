#pragma once
// ボスのレリックの3択（選ぶ／受け取らない）。候補はセーブデータ（bossRelicChoices）にあるので、窓に開閉の状態は無い。
// 開いているかは SceneManager::BossRelicPickOpen（フィールドで候補がある時）
#include <string>
#include "URect.h"
#include "UiContext.h"

class BossRelicPanel
{
public:
    void HandleInput(UiContext& ui);
    void Draw(UiContext& ui);

private:
    URect CardRect(int i, int n) const
    {
        const float w = 300.0f, h = 310.0f, gap = 30.0f;
        float total = n * w + (n - 1) * gap;
        return { (m_sw - total) / 2.0f + i * (w + gap), 170.0f, w, h };
    }
    URect SkipRect() const { return { m_sw / 2.0f - 90.0f, 520.0f, 180.0f, 44.0f }; }
    int  m_sw = 1280, m_sh = 720;
    std::string m_lastHoverKey;   // ホバー音を重ねないためのキー
};
