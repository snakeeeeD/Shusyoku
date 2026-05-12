#pragma once
#include "CardData.h"
#include <unordered_map>

class CardDataBase
{
public:
    static void Init();
    static const CardData* Get(const std::string& id);

private:
    static std::unordered_map<std::string, CardData> m_data;
};