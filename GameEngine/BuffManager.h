#pragma once
#include "Buff.h"
#include <vector>

class BuffManager
{
public:
    BuffManager();

    void AddBuff(const Buff& buff);
    void RemoveBuff(BuffType type);
    void OnTurnEnd();              // ターン終了時に期間を更新

    bool  HasBuff(BuffType type) const;
    int   GetBuffValue(BuffType type) const;
    const std::vector<Buff>& GetBuffs() const { return m_buffs; }

    // 各バフの効果を適用
    int  ApplyAttackBuff(int baseAttack) const;
    int ApplyBlockBuff(int baseBlock) const;
    int  ApplyMoveRangeBuff(int baseRange) const;
    int  GetRegenValue() const;

    // デバフ適用
    int  ApplyAttackDebuff(int baseAttack) const;
    int  ApplyDefenseDebuff(int baseBlock) const;

    struct TurnEndDamage {
        int poison = 0;
        // 後々ここにダメージデバフを追加
        int total() const { return poison; }
    };

    TurnEndDamage GetTurnEndDamage() const;

    // バフ・デバフ両方考慮した最終値
    int  GetFinalAttack(int baseAttack) const;
    int  GetFinalBlock(int baseBlock) const;

private:
    std::vector<Buff> m_buffs;
};