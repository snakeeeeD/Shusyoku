#pragma once
#include "EnemyData.h"
#include <unordered_map>

class EnemyDataBase
{
public:
    static void Init();
    static const EnemyData* Get(const std::string& id);

private:
    static void LoadHardcodedData();
    static std::unordered_map<std::string, EnemyData> m_data;
};