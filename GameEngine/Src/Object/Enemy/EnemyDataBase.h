#pragma once
#include "EnemyData.h"
#include <unordered_map>

class EnemyDataBase
{
public:
    static void Init();
    static const EnemyData* Get(const std::string& id);

    static void Reload();

private:
    static std::unordered_map<std::string, EnemyData> m_data;
};