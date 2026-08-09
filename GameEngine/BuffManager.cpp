#include "BuffManager.h"
#include <algorithm>

using std::max;

BuffManager::BuffManager() {}

void BuffManager::AddBuff(const Buff& buff)
{
    // 値加算スタック（毒・攻撃UP等）
    if (buff.type == BuffType::Poison ||
        buff.type == BuffType::AttackUp ||
        buff.type == BuffType::AttackUpTurn ||
        buff.type == BuffType::AttackGrowth ||
        buff.type == BuffType::Frenzy ||
        buff.type == BuffType::RangeUp ||
        buff.type == BuffType::NoxiousFumes ||
        buff.type == BuffType::ToxicRhythm)
    {
        for (auto& b : m_buffs)
            if (b.type == buff.type) { b.value += buff.value; return; }
        m_buffs.push_back(buff);
        return;
    }

    // 二値デバフ：重ねがけで持続を延長
    if (buff.type == BuffType::Weak ||
        buff.type == BuffType::Vulnerable ||
        buff.type == BuffType::Frail ||
        buff.type == BuffType::Root)
    {
        for (auto& b : m_buffs)
            if (b.type == buff.type) { b.duration += buff.duration; return; }
        m_buffs.push_back(buff);
        return;
    }

    // それ以外は上書き
    for (auto& b : m_buffs)
        if (b.type == buff.type)
        {
            b.value = buff.value;
            b.duration = buff.duration;
            return;
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

void BuffManager::OnTurnEnd(bool decrementPoison)
{
    if (decrementPoison)
        for (auto& b : m_buffs)
            if (b.type == BuffType::Poison) b.value--;

    for (auto& buff : m_buffs)
        if (buff.type != BuffType::Poison && !buff.isPermanent())
            buff.duration--;

    m_buffs.erase(
        std::remove_if(m_buffs.begin(), m_buffs.end(),
            [decrementPoison](const Buff& b)
            {
                if (b.type == BuffType::Poison) return decrementPoison && b.value <= 0;
                return !b.isPermanent() && b.duration <= 0;
            }),
        m_buffs.end());
}

int BuffManager::TickPoison()
{
    int dealt = 0; bool remove = false;
    for (auto& b : m_buffs)
        if (b.type == BuffType::Poison) { dealt = b.value; b.value--; if (b.value <= 0) remove = true; break; }
    if (remove) RemoveBuff(BuffType::Poison);
    return dealt;
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
    value += GetBuffValue(BuffType::AttackUpTurn);
    value -= GetBuffValue(BuffType::AttackDown);

    if (int fr = GetBuffValue(BuffType::Frenzy))
        value = value * (100 + 50 * fr) / 100;   // 狂乱：1枚ごとに+50%
    if (HasBuff(BuffType::Weak))   value = value * 75 / 100;
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