#pragma once
#include "EncounterData.h"
#include <vector>

class EncounterDataBase
{
public:
    static void Init();
    static const EncounterData* GetRandom(int rank);

    static const EncounterData* GetByIndex(int index);
    static int GetCount();
    static int GetCountByRank(int rank);

    static void Reload();

private:
    static std::vector<EncounterData> m_data;
};