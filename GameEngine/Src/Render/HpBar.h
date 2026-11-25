#pragma once
#include "SpriteRenderer.h"
#include <DirectXMath.h>
#include <cmath>

struct HPBarInfo
{
    int   currentHP = 0;
    int   maxHP = 1;
    float displayHP = 0.0f;       // 減少アニメ中の表示HP（実HPより大きい間、オレンジで残る）
    int   block = 0;
    int   poisonDmg = 0;
    bool  poisonBoosted = false;  // ToxicRhythmで毒が多段化しているか
    bool  hasBurn = false;
};

// HPバー（自分・敵パネル・足元・イベントの結果で共通）
class HpBar
{
public:
    // 表示HPを実HPへ減らしていく（差が大きいほど速く、最低でも毎秒minSpeed）
    static float StepDisplay(float display, int hp, float dt)
    {
        const float speed = 0.5f, minSpeed = 15.0f;
        if (display > (float)hp)
        {
            float rate = speed * (display - hp);
            if (rate < minSpeed) rate = minSpeed;
            display -= rate * dt;
            if (display < (float)hp) display = (float)hp;
        }
        return display;
    }

    static void Draw(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        float x, float y, float w, float h, const HPBarInfo& info, float time)
    {
        using namespace DirectX;
        float hpRatio = (float)info.currentHP / (float)info.maxHP;
        float displayRatio = info.displayHP / (float)info.maxHP;
        float poisonRatio = (float)info.poisonDmg / (float)info.maxHP;

        // ブロック中：外側を青白く脈動
        if (info.block > 0)
        {
            float glow = 0.7f + 0.3f * sinf(time * 3.0f);
            sr->DrawSprite(white, x - 4.0f, y - 4.0f, w + 8.0f, h + 8.0f,
                0.0f, XMFLOAT4(0.55f * glow, 0.75f * glow, 1.0f * glow, 0.9f));
        }

        // 枠（暗い縁）と溝（くすんだ赤）
        sr->DrawSprite(white, x - 2.0f, y - 2.0f, w + 4.0f, h + 4.0f, 0.0f, XMFLOAT4(0.06f, 0.05f, 0.08f, 1.0f));
        sr->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.22f, 0.05f, 0.06f, 1.0f));

        // 減少アニメーション（オレンジ）
        if (displayRatio > hpRatio)
            sr->DrawSprite(white, x, y, w * displayRatio, h, 0.0f, XMFLOAT4(0.9f, 0.6f, 0.1f, 1.0f));

        // 本体：ブロック中=青 / 火傷=明滅 / 通常=緑
        XMFLOAT4 barColor(0.20f, 0.78f, 0.30f, 1.0f);
        if (info.block > 0) barColor = XMFLOAT4(0.3f, 0.5f, 1.0f, 1.0f);
        else if (info.hasBurn)
        {
            float flash = 0.5f + 0.5f * sinf(time * 4.0f);
            barColor = XMFLOAT4(0.8f, 0.4f * flash, 0.0f, 1.0f);
        }
        float fw = w * hpRatio;
        sr->DrawSprite(white, x, y, fw, h, 0.0f, barColor);
        // 立体感：上に光沢、下に影
        sr->DrawSprite(white, x, y, fw, h * 0.35f, 0.0f, XMFLOAT4(1, 1, 1, 0.22f));
        sr->DrawSprite(white, x, y + h * 0.7f, fw, h * 0.3f, 0.0f, XMFLOAT4(0, 0, 0, 0.22f));

        // 毒ダメージ予測（紫。長さ＝ToxicRhythm込みの合計）
        if (info.poisonDmg > 0 && hpRatio > 0.0f)
        {
            float poisonW = w * (poisonRatio < hpRatio ? poisonRatio : hpRatio);
            float poisonX = x + w * hpRatio - poisonW;
            float alpha = 0.6f + 0.4f * sinf(time * 4.0f);
            sr->DrawSprite(white, poisonX, y, poisonW, h, 0.0f, XMFLOAT4(0.5f * alpha, 0.0f, 0.8f * alpha, alpha));
        }
    }
};