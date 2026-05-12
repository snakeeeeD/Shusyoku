#include "CardDataBase.h"

std::unordered_map<std::string, CardData> CardDataBase::m_data;

void CardDataBase::Init()
{
    m_data["strike"] = { "strike", L"ストライク",   CardType::Attack, 1, 6, 1, RangeType::Adjacent, "隣接した敵に6ダメージ" };
    m_data["defend"] = { "defend", L"ディフェンド", CardType::Skill,  1, 5, 0, RangeType::None,  "5ブロックを得る" };
    m_data["move"]  =  { "move",   L"ステップ",     CardType::Move,   1, 2, 1, RangeType::Adjacent,"2マス移動できる" };
}

const CardData* CardDataBase::Get(const std::string& id)
{
    auto it = m_data.find(id);
    if (it == m_data.end()) return nullptr;
    return &it->second;
}