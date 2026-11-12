#pragma once
#include "Buff.h"
#include <vector>

class BuffManager
{
public:
    BuffManager();

    void AddBuff(const Buff& buff);
    void RemoveBuff(BuffType type);
    void OnTurnEnd(bool decrementPoison = true);

    int  TickPoison();

    bool  HasBuff(BuffType type) const;
    int   GetBuffValue(BuffType type) const;
    const std::vector<Buff>& GetBuffs() const { return m_buffs; }   // 残りターン違いの同じ種類は別々に入っている
    std::vector<Buff> GetBuffsSummed() const;
    struct TimedPart { int value; int turns; };              // 期限つきの分（値と残りターン）
    std::vector<TimedPart> TimedParts(BuffType t) const;     // その種類の期限つきの分（残りの短い順）                       // 表示用：同じ種類を1つにまとめたもの

    // 最終値（バフ+デバフ込み）
    int  GetFinalAttack(int baseAttack) const;
    int  GetFinalBlock(int baseBlock) const;
    int  GetFinalMoveRange(int baseRange) const;

    struct TurnEndDamage {
        int poison = 0;
        int total() const { return poison; }
    };

    TurnEndDamage GetTurnEndDamage() const;

private:
    std::vector<Buff> m_buffs;
};