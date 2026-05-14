#include "CardDataBase.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <windows.h>

using json = nlohmann::json;

std::unordered_map<std::string, CardData> CardDataBase::m_data;

// 文字列からCardTypeに変換
static CardType StringToCardType(const std::string& str)
{
    if (str == "Attack") return CardType::Attack;
    if (str == "Skill")  return CardType::Skill;
    if (str == "Move")   return CardType::Move;
    if (str == "Power")  return CardType::Power;
    return CardType::Attack;
}

// 文字列からRangeTypeに変換
static RangeType StringToRangeType(const std::string& str)
{
    if (str == "Adjacent") return RangeType::Adjacent;
    if (str == "Cross")    return RangeType::Cross;
    if (str == "Area")     return RangeType::Area;
    if (str == "Diamond")  return RangeType::Diamond;
    return RangeType::None;
}

// UTF-8からwstringに変換
static std::wstring ToWString(const std::string& str)
{
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}

void CardDataBase::Init()
{
    std::ifstream file("Assets/Data/cards.json");
    if (!file.is_open())
    {
        OutputDebugStringW(L"★ cards.json 読み込み失敗 - ハードコードデータを使用\n");
        LoadHardcodedData();
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
            data.value = c["value"];
            data.range = c["range"];
            data.rangeType = StringToRangeType(c["rangeType"]);
            data.description = ToWString(c["description"]);

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
        LoadHardcodedData();
    }
}

void CardDataBase::LoadHardcodedData()
{
    m_data["strike"] = { "strike", L"ストライク",   CardType::Attack, 1, 6, 1, RangeType::Adjacent, L"隣接した敵に6ダメージ" };
    m_data["defend"] = { "defend", L"ディフェンド", CardType::Skill,  1, 5, 0, RangeType::None,     L"5ブロックを得る" };
    m_data["move"] = { "move",   L"ステップ",     CardType::Move,   1, 2, 1, RangeType::Adjacent, L"隣接したマスに移動" };
}

const CardData* CardDataBase::Get(const std::string& id)
{
    auto it = m_data.find(id);
    if (it == m_data.end()) return nullptr;
    return &it->second;
}