#pragma once
#include "CardType.h"
#include <DirectXMath.h>
using namespace DirectX;

// グリッドのハイライト色を1箇所に集約
// 用途(意味)→色 の対応表。明るさは呼び出し側が Scale で掛ける
namespace HighlightPalette
{
    // --- 移動カード ---
    inline constexpr XMFLOAT4 MoveRange{ 0.2f, 1.0f, 0.4f, 1.0f };   // 到達可能
    inline constexpr XMFLOAT4 MovePath{ 0.3f, 0.85f, 0.5f, 1.0f };  // 経路
    inline constexpr XMFLOAT4 Unreachable{ 0.25f, 0.25f, 0.25f, 1.0f };// 到達不可
    inline constexpr XMFLOAT4 OutOfRange{ 0.3f, 0.3f, 0.3f, 1.0f };   // 射程外

    // --- カード種別の範囲 ---
    inline constexpr XMFLOAT4 AttackRange{ 1.0f, 0.2f, 0.2f, 1.0f };
    inline constexpr XMFLOAT4 AttackLine{ 0.35f, 0.15f, 0.15f, 1.0f };
    inline constexpr XMFLOAT4 SkillRange{ 0.2f, 0.2f, 1.0f, 1.0f };
    inline constexpr XMFLOAT4 PowerRange{ 1.0f, 0.2f, 1.0f, 1.0f };

    // --- 危険（敵の攻撃） ---
    inline constexpr XMFLOAT4 Danger{ 1.0f, 1.0f, 0.1f, 1.0f };   // 移動プレビューの危険
    inline constexpr XMFLOAT4 Threat{ 1.0f, 0.75f, 0.0f, 1.0f };  // 敵の危険（波紋）
    inline constexpr XMFLOAT4 ThreatFocus{ 1.0f, 1.0f, 0.35f, 1.0f };  // 選択中の敵

    // --- その他 ---
    inline constexpr XMFLOAT4 EnemyNormal{ 1.0f, 1.0f, 1.0f, 1.0f };   // 敵本体リセット

    // 明るさを掛ける（アルファは保持）
    inline XMFLOAT4 Scale(const XMFLOAT4& c, float k)
    {
        return XMFLOAT4(c.x * k, c.y * k, c.z * k, c.w);
    }

    // カード種別 → 範囲の基準色
    inline XMFLOAT4 ForCard(CardType type)
    {
        switch (type)
        {
        case CardType::Attack: return AttackRange;
        case CardType::Move:   return MoveRange;
        case CardType::Skill:  return SkillRange;
        case CardType::Power:  return PowerRange;
        default:               return XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        }
    }
}