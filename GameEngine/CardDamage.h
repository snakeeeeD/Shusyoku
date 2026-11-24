#pragma once
#include "CardData.h"
#include "Player.h"
#include "RelicManager.h"
#include <algorithm>

// カード1枚の攻撃のダメージ（1発ぶん）。表示（CardVisual）と実際（CardExecutor）の両方がここを呼ぶ。
// 新しい上乗せ（バフ・レリック）を足すときは、ここだけに書く（表示と実際がずれないように）
namespace CardDamage
{
    // 攻撃UP 以外の上乗せ：信仰・移動マス数・失ったHP・背水（HP半分以下／1/4以下）・ナイフ
    inline int Flat(const CardData& d, const Player& p)
    {
        int v = 0;
        if (d.scaleByFaith) v += p.GetFeetFaith();                          // 聖なる刃：足もとの信仰
        v += p.GetMoved() * d.movedBonus;                                    // 移動マス数で増える
        int hp = p.GetHp(), mhp = p.GetMaxHp();
        v += (mhp - hp) * d.missingHpPct / 100;                              // 血刃：失ったHPの%
        v += p.GetExhaustCount() * d.perExhaust;                             // 灰燼撃：廃棄札1枚につき
        if (hp * 2 <= mhp) v += p.GetBuffManager().GetBuffValue(BuffType::LastStand);   // 背水：半分以下
        if (hp * 4 <= mhp) v += p.GetBuffManager().GetBuffValue(BuffType::DeepStand);   // 1/4以下
        if (std::find(d.tags.begin(), d.tags.end(), "Knife") != d.tags.end())
            v += p.GetBuffManager().GetBuffValue(BuffType::KnifePower) + RelicManager::SumValue("knifeBonus");   // 研ぎ師
        return v;
    }

    // 1発のダメージ。baseValue＝元の値（罠の数を掛けた後など。ふつうは mainEffect.value）
    inline int Compute(const CardData& d, const Player& p, int baseValue)
    {
        return p.GetBuffManager().GetFinalAttack(baseValue) + Flat(d, p);
    }
}
