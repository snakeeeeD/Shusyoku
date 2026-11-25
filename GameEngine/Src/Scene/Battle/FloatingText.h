#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <DirectXMath.h>

struct FloatingText
{
    std::wstring text;
    float worldX, worldY, worldZ;
    float offsetX;                    // 同時発生時に重ならないよう左右にばらす
    DirectX::XMFLOAT4 color;
    float size;
    float timer = 0.0f;
};

// 数字ポップアップ。ワールド座標で保持し、描画時に射影する
class FloatingTextManager
{
public:
    static constexpr float DURATION = 1.2f;
    static constexpr float RISE = 1.0f;          // 上昇量（ワールド単位）

    static void Spawn(float wx, float wy, float wz, const std::wstring& text,
        const DirectX::XMFLOAT4& color, float size = 28.0f)
    {
        FloatingText t;
        t.text = text;
        t.worldX = wx; t.worldZ = wz;
        t.offsetX = (float)(rand() % 41 - 20);
        t.color = color;
        t.size = size;

        // 同じ場所に既にある数字ぶん上へ逃がす（push_backより前に）
        float stack = 0.0f;
        for (auto& o : m_texts)
            if (fabsf(o.worldX - wx) < 0.3f && fabsf(o.worldZ - wz) < 0.3f)
                stack += 0.45f;
        t.worldY = wy + stack;

        m_texts.push_back(t);
    }

    // ダメージ用（HPが減った分は赤、全部ブロックされたら青）
    static void SpawnDamage(float wx, float wy, float wz, int dmg, int blocked)
    {
        if (dmg > 0)
            Spawn(wx, wy, wz, std::to_wstring(dmg),
                DirectX::XMFLOAT4(1.0f, 0.3f, 0.2f, 1.0f), 44.0f);
        else if (blocked > 0)
            Spawn(wx, wy, wz, std::to_wstring(blocked),
                DirectX::XMFLOAT4(0.4f, 0.7f, 1.0f, 1.0f), 32.0f);
    }

    static void Update(float deltaTime)
    {
        for (int i = (int)m_texts.size() - 1; i >= 0; i--)
        {
            m_texts[i].timer += deltaTime;
            if (m_texts[i].timer >= DURATION)
                m_texts.erase(m_texts.begin() + i);
        }
    }

    static void Clear() { m_texts.clear(); }
    static const std::vector<FloatingText>& GetAll() { return m_texts; }

    // 見た目の計算はここに集約（描画側は従うだけ）
    static void GetVisual(const FloatingText& t, float& outRise, float& outAlpha, float& outSize)
    {
        float r = t.timer / DURATION;
        float e = 1.0f - (1.0f - r) * (1.0f - r);              // 上昇はease out
        outRise = RISE * e;
        outAlpha = (r < 0.7f) ? 1.0f : (1.0f - r) / 0.3f;      // 終盤だけフェード
        float pop = (r < 0.15f) ? (1.6f - 4.0f * r) : 1.0f;    // 出た瞬間だけ大きく
        outSize = t.size * pop;
    }

private:
    static inline std::vector<FloatingText> m_texts;
};