#include "EnemyDataBase.h"
#include "GameUtils.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

using json = nlohmann::json;

std::unordered_map<std::string, EnemyData> EnemyDataBase::m_data;


void EnemyDataBase::Init()
{

    OutputDebugStringW(L"★ EnemyDataBase初期化開始\n");

    // JSONファイルから読み込み
    std::ifstream file("Assets/Data/enemies.json");

    // カレントディレクトリを確認
    char path[256];
    GetCurrentDirectoryA(256, path);
    char buf[512];
    sprintf_s(buf, "★ カレントディレクトリ: %s\n", path);
    OutputDebugStringA(buf);

   // std::ifstream file("Assets/Data/enemies.json");
    if (!file.is_open())
    {
        OutputDebugStringW(L"★ enemies.json 読み込み失敗\n");
        return;
    }

    OutputDebugStringW(L"★ enemies.json 読み込み成功\n");

    try
    {
        // ファイル内容を読み込み
        std::string content((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());

        // デバッグ出力
        OutputDebugStringA("★ JSONファイル内容:\n");
        OutputDebugStringA(content.c_str());
        OutputDebugStringA("\n");

        // パース
        json j = json::parse(content);
        OutputDebugStringW(L"★ JSONパース成功\n");

        // データ読み込み
        if (j.contains("enemies") && j["enemies"].is_array())
        {
            for (const auto& e : j["enemies"])
            {
                EnemyData data;
                data.id = e["id"];
                data.textureName = e["texture"];
                data.hp = e["hp"];
                data.attack = e.value("attack", 0);
                data.width = e["width"];
                data.height = e["height"];

                // 行動データの読み込み
                if (e.contains("actions") && e["actions"].is_array())
                {
                    for (const auto& actionJson : e["actions"])
                    {
                        EnemyAction action;

                        // typeを文字列から変換
                        std::string typeStr = actionJson["type"];
                        action.type = StringToActionType(typeStr);

                        action.value = actionJson["value"];
                        action.range = actionJson["range"];
                        action.rangeType = StringToRangeType(actionJson.value("rangeType", "Adjacent"));
                        action.chance = actionJson["chance"];

                        // rangeType を読み込む
                        if (actionJson.contains("rangeType"))
                        {
                            std::string rangeTypeStr = actionJson["rangeType"];
                            action.rangeType = StringToRangeType(rangeTypeStr);
                        }
                        else
                        {
                            action.rangeType = RangeType::Adjacent;  // デフォルト
                        }

                        // descriptionをUTF-8からwstringに変換
                        std::string descStr = actionJson["description"];
                        action.description = ToWString(descStr);

                        data.actions.push_back(action);
                    }
                }

                m_data[data.id] = data;

                // 読み込み確認
                wchar_t buf[256];
                swprintf_s(buf, L"★ 敵データ読み込み: %S (HP:%d, 行動数:%d)\n",
                    data.id.c_str(), data.hp, (int)data.actions.size());
                OutputDebugStringW(buf);
            }

            OutputDebugStringW(L"★ EnemyDataBase初期化完了（JSONから読み込み）\n");
        }
        else
        {
            OutputDebugStringW(L"★ JSON構造エラー\n");
        }
    }
    catch (const json::parse_error& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONパースエラー: %s (位置: %d)\n", e.what(), (int)e.byte);
        OutputDebugStringA(buf);
    }
    catch (const json::exception& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONエラー: %s\n", e.what());
        OutputDebugStringA(buf);
    }
    catch (...)
    {
        OutputDebugStringW(L"★ 不明なエラー\n");
    }
}

const EnemyData* EnemyDataBase::Get(const std::string& id)
{
    auto it = m_data.find(id);
    if (it == m_data.end())
    {
        wchar_t buf[256];
        swprintf_s(buf, L"★ エラー: 敵データが見つかりません: %S\n", id.c_str());
        OutputDebugStringW(buf);
        return nullptr;
    }
    return &it->second;
}