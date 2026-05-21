#include "BuffManager.h"
#include <algorithm>

using std::max;

BuffManager::BuffManager() {}

void BuffManager::AddBuff(const Buff& buff)
{
    // 同じ種類のバフがあれば上書き
    for (auto& b : m_buffs)
    {
        if (b.type == buff.type)
        {
            b.value = buff.value;
            b.duration = buff.duration;
            return;
        }
    }
    m_buffs.push_back(buff);
}

void BuffManager::RemoveBuff(BuffType type)
{
    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [type](const Buff& b) { return b.type == type; }),
        m_buffs.end()
    );
}

void BuffManager::OnTurnEnd()
{
    for (auto& buff : m_buffs)
    {
        if (!buff.isPermanent())
            buff.duration--;
    }

    // 期間切れのバフを削除
    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [](const Buff& b) { return !b.isPermanent() && b.duration <= 0; }),
        m_buffs.end()
    );
}

bool BuffManager::HasBuff(BuffType type) const
{
    for (auto& b : m_buffs)
        if (b.type == type) return true;
    return false;
}

int BuffManager::GetBuffValue(BuffType type) const
{
    for (auto& b : m_buffs)
        if (b.type == type) return b.value;
    return 0;
}

int BuffManager::ApplyAttackBuff(int baseAttack) const
{
    return baseAttack + GetBuffValue(BuffType::AttackUp);
}

int BuffManager::ApplyBlockBuff(int baseBlock) const
{
    return baseBlock + GetBuffValue(BuffType::DefenseUp);
}

int BuffManager::ApplyMoveRangeBuff(int baseRange) const
{
    return baseRange + GetBuffValue(BuffType::MoveUp);
}

int BuffManager::GetRegenValue() const
{
    return GetBuffValue(BuffType::Regeneration);
}

int BuffManager::ApplyAttackDebuff(int baseAttack) const
{
    return max(0, baseAttack - GetBuffValue(BuffType::AttackDown));
}

int BuffManager::ApplyDefenseDebuff(int baseBlock) const
{
    return max(0, baseBlock - GetBuffValue(BuffType::DefenseDown));
}

int BuffManager::GetPoisonValue() const
{
    return GetBuffValue(BuffType::Poison);
}

// バフ・デバフ両方考慮した最終値
int BuffManager::GetFinalAttack(int baseAttack) const
{
    int value = baseAttack;
    value += GetBuffValue(BuffType::AttackUp);
    value -= GetBuffValue(BuffType::AttackDown);
    return max(0, value);
}

int BuffManager::GetFinalBlock(int baseBlock) const
{
    int value = baseBlock;
    value += GetBuffValue(BuffType::DefenseUp);
    value -= GetBuffValue(BuffType::DefenseDown);
    return max(0, value);
}