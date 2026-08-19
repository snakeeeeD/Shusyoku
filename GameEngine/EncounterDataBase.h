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

    static const EncounterData* GetEncounter(int layer, EncCategory cat, int tier, int seed);
    static const EncounterData* GetById(const std::string& id);
    static const std::vector<EscalationTier>& DefaultEscalation();

    static void ResetLastPick();

private:
    static std::vector<EncounterData> m_data;
};