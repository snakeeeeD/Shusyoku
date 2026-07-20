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
        case DamageFeel::Poison:
        {
            XMFLOAT4 pc = BuffInfo::Get(BuffType::Poison).color;
            EffectManager::SpawnBurst(x, y, z, 10, 0.8f,
                XMFLOAT4(pc.x, pc.y, pc.z, 0.9f),                       // 開始：毒の色
                XMFLOAT4(pc.x * 0.4f, pc.y * 0.4f, pc.z * 0.5f, 0.0f),  // 終了：暗く・透明
                0.9f, 0.10f);
            break;
        }
        case DamageFeel::Burn:
        {
            XMFLOAT4 bc = BuffInfo::Get(BuffType::Burn).color;
            EffectManager::SpawnBurst(x, y, z, 12, 1.2f,
                XMFLOAT4(bc.x, bc.y, bc.z, 1.0f),                       // 開始：炎の色
                XMFLOAT4(bc.x * 0.5f, bc.y * 0.2f, bc.z * 0.1f, 0.0f),  // 終了：暗く・透明
                0.6f, 0.09f);
            break;
        }
        default:
            EffectManager::Play("hit", x, y, z);
            break;
        }
    }
}