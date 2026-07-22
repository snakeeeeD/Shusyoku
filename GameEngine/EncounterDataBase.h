#pragma once
#include "EncounterData.h"
#include <vector>

class EncounterDataBase
{
public:
    static void Init();

    static const EncounterData* GetByIndex(int index);
    static int GetCount();

    static void Reload();

    static const EncounterData* GetEncounter(int layer, EncCategory cat, int seed);
    static const std::vector<EscalationTier>& DefaultEscalation();

private:
    static std::vector<EncounterData> m_data;
};