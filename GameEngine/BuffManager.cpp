#include "BuffManager.h"
#include <algorithm>

using std::max;

BuffManager::BuffManager() {}

void BuffManager::AddBuff(const Buff& buff)
{
    // 毒・攻撃UPはスタック（加算）
    if (buff.type == BuffType::Poison || 
        buff.type == BuffType::AttackUp || 
        buff.type == BuffType::RangeUp)
    {
        for (auto& b : m_buffs)
        {
            if (b.type == buff.type)
            {
                b.value += buff.value;
                return;
            }
        }
        m_buffs.push_back(buff);
        return;
    }

    // それ以外は同種上書き
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
    // 毒：ダメージ後に1減少
    for (auto& b : m_buffs)
    {
        if (b.type == BuffType::Poison)
            b.value--;
    }

    // その他：duration減少
    for (auto& buff : m_buffs)
    {
        if (buff.type != BuffType::Poison && !buff.isPermanent())
            buff.duration--;
    }

    // 期限切れ削除（毒はvalue<=0、他はduration<=0）
    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [](const Buff& b)
            {
                if (b.type == BuffType::Poison)
                    return b.value <= 0;
                return !b.isPermanent() && b.duration <= 0;
            }),
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

BuffManager::TurnEndDamage BuffManager::GetTurnEndDamage() const
{
    TurnEndDamage dmg;
    dmg.poison = GetBuffValue(BuffType::Poison);
    return dmg;
}

int BuffManager::GetFinalAttack(int baseAttack) const
{
    int value = baseAttack;
    value += GetBuffValue(BuffType::AttackUp);
    value -= GetBuffValue(BuffType::AttackDown);

    // Weak: 25%減
    if (HasBuff(BuffType::Weak))
        value = value * 75 / 100;

    return max(0, value);
}

int BuffManager::GetFinalBlock(int baseBlock) const
{
    int value = baseBlock;
    value += GetBuffValue(BuffType::DefenseUp);
    value -= GetBuffValue(BuffType::DefenseDown);

    // Frail: 25%減
    if (HasBuff(BuffType::Frail))
        value = value * 75 / 100;

    return max(0, value);
}

int BuffManager::GetFinalMoveRange(int baseRange) const
{
    if (HasBuff(BuffType::Root))
        return 0;

    int value = baseRange;
    value += GetBuffValue(BuffType::MoveUp);
    value -= GetBuffValue(BuffType::Slow);

    return max(0, value);
}