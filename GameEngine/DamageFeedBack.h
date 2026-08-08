#pragma once
#include <DirectXMath.h>
#include <string>
#include "EffectManager.h"
#include "FloatingText.h"
#include "BuffInfo.h"
#include "BuffType.h"

using namespace DirectX;

// ダメージの質感。既定はHit（殴打）。DoTはそれぞれの見た目に
enum class DamageFeel { Hit, Poison, Burn };

// 被ダメージの世界座標フィードバック（数字＋パーティクル）を1箇所に集約
namespace DamageFeedback
{
    inline void Play(DamageFeel feel, float x, float y, float z, int dmg, int blocked)
    {
        // ダメージ数字
        if (dmg > 0)
        {
            XMFLOAT4 col;
            switch (feel)
            {
            case DamageFeel::Poison: col = BuffInfo::Get(BuffType::Poison).color; break;    // 紫
            case DamageFeel::Burn: col = BuffInfo::Get(BuffType::Burn).color; break;        // オレンジ
            default:                 col = XMFLOAT4(1.0f, 0.3f, 0.2f, 1.0f); break;         // 赤
            }
            FloatingTextManager::Spawn(x, y, z, std::to_wstring(dmg), col, 44.0f);
        }
        else if (blocked > 0)
        {
            FloatingTextManager::Spawn(x, y, z, std::to_wstring(blocked),
                XMFLOAT4(0.4f, 0.7f, 1.0f, 1.0f), 32.0f);
        }

        // パーティクル
        if (dmg <= 0) return;
        switch (feel)
        {
        case DamageFeel::Poison: EffectManager::Play("poison", x, y, z); break;
        case DamageFeel::Burn:   EffectManager::Play("burn", x, y, z + 0.4f); break;
        default:                 EffectManager::Play("hit", x, y, z); break;
        }
    }
}