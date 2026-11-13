#include "BuffManager.h"
#include <algorithm>

using std::max;

BuffManager::BuffManager() {}

void BuffManager::AddBuff(const Buff& buff)
{
    // 値加算スタック（毒・攻撃UP等）
    if (buff.type == BuffType::Poison ||
        buff.type == BuffType::AttackUp ||
        buff.type == BuffType::DefenseUp ||      // 攻撃UPとそろえて足し合わせる（前は上書き）
        buff.type == BuffType::AttackUpTurn ||
        buff.type == BuffType::AttackGrowth ||
        buff.type == BuffType::Frenzy ||
        buff.type == BuffType::Momentum ||
        buff.type == BuffType::Charge ||
        buff.type == BuffType::HitAndRun ||
        buff.type == BuffType::RangeUp ||
        buff.type == BuffType::NoxiousFumes ||
        buff.type == BuffType::ToxicRhythm || 
        buff.type == BuffType::Thorns || 
        buff.type == BuffType::Riposte ||
        buff.type == BuffType::KnifePower ||
        buff.type == BuffType::CardBlock || 
        buff.type == BuffType::KnifeGen || 
        buff.type == BuffType::LastStand || 
        buff.type == BuffType::DeepStand ||
        buff.type == BuffType::GuardianVow ||
        buff.type == BuffType::Cathedral ||
        buff.type == BuffType::StandFirm ||   // 不退転は重ねると%が足される
        buff.type == BuffType::BloodThirst ||
        buff.type == BuffType::Berserker ||
        buff.type == BuffType::BloodFlash)
    {
        // 残りターンが同じものだけまとめる。違えば別に持つ＝それぞれのターンで切れる
        //（永続と期限つきが混ざっても、期限つきの分だけが切れる。毒は値で減るのでいつもまとめる）
        for (size_t i = 0; i < m_buffs.size(); i++)
        {
            Buff& b = m_buffs[i];
            if (b.type != buff.type) continue;
            if (buff.type != BuffType::Poison && b.duration != buff.duration) continue;
            b.value += buff.value;
            if (b.value == 0 && buff.type != BuffType::Poison)
                m_buffs.erase(m_buffs.begin() + i);   // 足し引きで0になったら消す（先制の一瞬の加算など）
            return;
        }
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
    int v = 0;
    for (auto& b : m_buffs)
        if (b.type == type) v += b.value;   // 残りターン違いで分かれていても全部足す
    return v;
}

// その種類の期限つきの分（残りターンの短い順）。永続（-1・999以上）は入らない
std::vector<BuffManager::TimedPart> BuffManager::TimedParts(BuffType t) const
{
    std::vector<TimedPart> v;
    for (auto& b : m_buffs)
        if (b.type == t && !b.isPermanent() && b.duration < 999) v.push_back({ b.value, b.duration });
    std::sort(v.begin(), v.end(), [](const TimedPart& a, const TimedPart& b) { return a.turns < b.turns; });
    return v;
}

// 表示用：同じ種類を1つにまとめる（値は合計。どれかが永続なら永続、そうでなければ長い方の残りターン）
std::vector<Buff> BuffManager::GetBuffsSummed() const
{
    std::vector<Buff> out;
    for (auto& b : m_buffs)
    {
        auto it = std::find_if(out.begin(), out.end(), [&](const Buff& o) { return o.type == b.type; });
        if (it == out.end()) { out.push_back(b); continue; }
        it->value += b.value;
        if (it->isPermanent() || b.isPermanent()) it->duration = -1;
        else if (b.duration > it->duration)       it->duration = b.duration;
    }
    return out;
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
    value += GetBuffValue(BuffType::HolyGround);   // 聖地に立っている間の加算
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