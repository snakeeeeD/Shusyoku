#pragma once
#include "CardData.h"
#include <unordered_map>

class CardDataBase
{
public:
    static void Init();
    static const CardData* Get(const std::string& id);

    static const std::unordered_map<std::string, CardData>& GetAll() { return m_data; }
    static CardData BuildCrafted(const std::string& id);

    static std::vector<std::string> RewardPool();
    static std::string RandomCard();

private:
    static void LoadHardcodedData();
    static std::unordered_map<std::string, CardData> m_data;
};