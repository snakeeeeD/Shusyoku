#pragma once
#include "BuffType.h"
#include <vector>
#include <cmath>
#include <DirectXMath.h>

// バフ・デバフが付いた時に、そのキャラの頭の上に出すアイコン（スレスパのように「何が付いたか」を見せる）。
// ワールドの位置で持ち、描く時に画面へ写す（FloatingText と同じ作り）。描くのは BattleUI::DrawBuffPopups
struct BuffPopup
{
    BuffType type;
    int amount = 0;                 // 増えた数（0なら数字は出さない＝値の無いバフ）
    float worldX, worldY, worldZ;   // 頭の上
    float offsetX = 0.0f;           // 同じ時に何個も付いたら、横に並べる（画面のピクセル）
    float timer = 0.0f;
};

class BuffPopupManager
{
public:
    static constexpr float DURATION = 0.9f;    // 出てから消えるまで
    static constexpr float RISE = 0.45f;       // 浮く高さ（ワールド）
    static constexpr float ICON = 40.0f;       // アイコンの大きさ（画面のピクセル）
    static constexpr float GAP = 46.0f;        // 横に並べる間隔

    static void Spawn(BuffType type, int amount, float wx, float wy, float wz)
    {
        // 同じキャラに、ほぼ同じ時に出たものは横に並べる（全体が真ん中にそろうよう、前のものも左へずらす）
        std::vector<BuffPopup*> same;
        for (auto& p : s_popups)
            if (p.timer < 0.05f && fabsf(p.worldX - wx) < 0.3f && fabsf(p.worldZ - wz) < 0.3f) same.push_back(&p);
        BuffPopup b; b.type = type; b.amount = amount; b.worldX = wx; b.worldY = wy; b.worldZ = wz;
        b.offsetX = same.size() * GAP * 0.5f;
        for (auto* p : same) p->offsetX -= GAP * 0.5f;
        s_popups.push_back(b);
    }

    static void Update(float dt)
    {
        for (int i = (int)s_popups.size() - 1; i >= 0; i--)
            if ((s_popups[i].timer += dt) >= DURATION) s_popups.erase(s_popups.begin() + i);
    }

    static void Clear() { s_popups.clear(); }
    static const std::vector<BuffPopup>& GetAll() { return s_popups; }

    // 見た目：出た瞬間にぽんと大きく → 戻る。ゆっくり浮いて、終わりの3割で消える
    static void GetVisual(const BuffPopup& p, float& outRise, float& outAlpha, float& outScale)
    {
        float r = p.timer / DURATION;
        outRise = RISE * (1.0f - (1.0f - r) * (1.0f - r));
        outAlpha = (r < 0.7f) ? 1.0f : (1.0f - r) / 0.3f;
        outScale = (r < 0.12f) ? 1.0f + 0.6f * (1.0f - r / 0.12f) : 1.0f;
    }

private:
    static inline std::vector<BuffPopup> s_popups;
};
