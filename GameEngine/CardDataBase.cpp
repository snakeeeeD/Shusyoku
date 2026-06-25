#include "CardDataBase.h"
#include "GameUtils.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <windows.h>

using json = nlohmann::json;

std::unordered_map<std::string, CardData> CardDataBase::m_data;

void CardDataBase::Init()
{
    std::ifstream file("Assets/Data/cards.json");
    if (!file.is_open())
    {
        OutputDebugStringW(L"★ cards.json 読み込み失敗\n");
        return;
    }

    OutputDebugStringW(L"★ cards.json 読み込み成功\n");

    try
    {
        json j;
        file >> j;

        for (const auto& c : j["cards"])
        {
            CardData data;
            data.id = c["id"];
            data.name = ToWString(c["name"]);
            data.type = StringToCardType(c["type"]);
            data.cost = c["cost"];
            data.range = c["range"];
            data.rangeType = StringToRangeType(c["rangeType"]);
            data.description = ToWString(c["description"]);

            std::string rarityStr = c.value("rarity", "Common");
            if (rarityStr == "Uncommon") data.rarity = CardRarity::Uncommon;
            else if (rarityStr == "Rare") data.rarity = CardRarity::Rare;
            else data.rarity = CardRarity::Common;

            // mainEffect
            data.mainEffect.hasEffect = true;
            data.mainEffect.type = StringToCardEffectType(c["mainEffect"]["type"]);
            data.mainEffect.value = c["mainEffect"]["value"];
            data.mainEffect.duration = c["mainEffect"].value("duration", 0);
            data.mainEffect.buffType = c["mainEffect"].value("buffType", "");


            if (c.contains("onHitEffect"))
            {
                data.onHitEffect.hasEffect = true;
                data.onHitEffect.type = StringToCardEffectType(c["onHitEffect"]["type"]);
                data.onHitEffect.value = c["onHitEffect"]["value"];
                data.onHitEffect.duration = c["onHitEffect"].value("duration", 0);
                data.onHitEffect.buffType = c["onHitEffect"].value("buffType", "");
            }
            else
            {
                data.onHitEffect.hasEffect = false;
            }

            m_data[data.id] = data;

            wchar_t buf[256];
            swprintf_s(buf, L"★ カード読み込み: %s\n", data.name.c_str());
            OutputDebugStringW(buf);
        }

        OutputDebugStringW(L"★ CardDataBase 初期化完了\n");
    }
    catch (const json::exception& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONエラー: %s\n", e.what());
        OutputDebugStringA(buf);
    }
}

const CardData* CardDataBase::Get(const std::string& id)
{
    auto it = m_data.find(id);
    if (it == m_data.end()) return nullptr;
    return &it->second;
}