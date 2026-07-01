#pragma once
#include "Buff.h"
#include <vector>

class BuffManager
{
public:
    BuffManager();

    void AddBuff(const Buff& buff);
    void RemoveBuff(BuffType type);
    void OnTurnEnd();

    bool  HasBuff(BuffType type) const;
    int   GetBuffValue(BuffType type) const;
    const std::vector<Buff>& GetBuffs() const { return m_buffs; }

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