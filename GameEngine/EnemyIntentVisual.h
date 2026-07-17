#pragma once
#include "EnemyAction.h"
#include "BuffManager.h"
#include <DirectXMath.h>

using namespace DirectX;

// 敵の行動予告の「見せ方」を1箇所に集約
class EnemyIntentVisual
{
public:
    // 予告に出す効果か
    static bool ShouldShow(const Effect& e)
    {
        if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway) return false;
        return e.value > 0 || !e.buff.empty();
    }

    static XMFLOAT4 GetIconColor(const Effect& e)
    {
        switch (e.kind)
        {
        case EffectKind::Damage: return XMFLOAT4(0.9f, 0.2f, 0.2f, 1.0f);
        case EffectKind::Block:  return XMFLOAT4(0.2f, 0.4f, 0.9f, 1.0f);
        case EffectKind::Buff:   return XMFLOAT4(1.0f, 0.8f, 0.0f, 1.0f);
        case EffectKind::Debuff: return XMFLOAT4(0.6f, 0.0f, 0.8f, 1.0f);
        default:                 return XMFLOAT4(0.5f, 0.5f, 0.5f, 1.0f);
        }
    }

    // バフ適用後の表示値
    static int GetDisplayValue(const Effect& e, const BuffManager& buffs)
    {
        if (e.kind == EffectKind::Damage) return buffs.GetFinalAttack(e.value);
        if (e.kind == EffectKind::Block)  return buffs.GetFinalBlock(e.value);
        return e.value;
    }

    // この行動はプレイヤーを害するか（危険表示の対象か）
    static bool IsHarmful(const EnemyAction& a)
    {
        for (auto& e : a.effects)
        {
            if (e.kind == EffectKind::Damage) return true;
            if (e.kind == EffectKind::Debuff && e.applyTo == ApplyTo::Player) return true;
        }
        return false;
    }

    // この行動がプレイヤーに与えるダメージ合計（バフ適用後）
    static int GetTotalDamage(const EnemyAction& a, const BuffManager& buffs)
    {
        int sum = 0;
        for (auto& e : a.effects)
            if (e.kind == EffectKind::Damage)
                sum += buffs.GetFinalAttack(e.value);
        return sum;
    }

    static constexpr float ICON_SIZE = 18.0f;
    static constexpr float STEP = ICON_SIZE + 24.0f;
};