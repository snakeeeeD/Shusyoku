
    // ハードコードされたデータを登録(Json実装前)
    /*{
        EnemyData data;
        data.id = "slime";
        data.textureName = "enemy_slime";
        data.hp = 20;
        data.attack = 3;
        data.width = 1.0f;
        data.height = 1.0f;
        data.actions = {
            { EnemyActionType::Attack, 3, 1, 70, L" 3ダメージ" },
            { EnemyActionType::Move,   0, 0, 30, L" 移動" },
        };
        m_data["slime"] = data;
    }
    {
        EnemyData data;
        data.id = "goblin";
        data.textureName = "enemy_goblin";
        data.hp = 40;
        data.attack = 8;
        data.width = 1.0f;
        data.height = 1.0f;
        data.actions = {
            { EnemyActionType::Attack, 8, 1, 60, L" 8ダメージ" },
            { EnemyActionType::Defend, 5, 0, 20, L" 5ブロック" },
            { EnemyActionType::Move,   0, 0, 20, L" 移動" },
        };
        m_data["goblin"] = data;
    }*/

#include "EnemyDataBase.h"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <string>

using json = nlohmann::json;

std::unordered_map<std::string, EnemyData> EnemyDataBase::m_data;

// 文字列からEnemyActionTypeに変換
EnemyActionType StringToActionType(const std::string& str)
{
    if (str == "Attack") return EnemyActionType::Attack;
    if (str == "Defend") return EnemyActionType::Defend;
    if (str == "Move") return EnemyActionType::Move;
    if (str == "Buf") return EnemyActionType::Buf;
    if (str == "Debuf") return EnemyActionType::Debuf;
    return EnemyActionType::Attack; // デフォルト
}

// std::stringからstd::wstringに変換
std::wstring StringToWString(const std::string& str)
{
    if (str.empty()) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

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
        OutputDebugStringW(L"★ enemies.json 読み込み失敗 - ハードコードデータを使用\n");
        LoadHardcodedData();
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
                        action.chance = actionJson["chance"];

                        // descriptionをUTF-8からwstringに変換
                        std::string descStr = actionJson["description"];
                        action.description = StringToWString(descStr);

                        data.actions.push_back(action);

                        // デバッグ出力
                        char buf[256];
                        sprintf_s(buf, "  - 行動: type=%s, value=%d, chance=%d%%\n",
                            typeStr.c_str(), action.value, action.chance);
                        OutputDebugStringA(buf);
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
            OutputDebugStringW(L"★ JSON構造エラー - ハードコードデータを使用\n");
            LoadHardcodedData();
        }
    }
    catch (const json::parse_error& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONパースエラー: %s (位置: %d)\n", e.what(), (int)e.byte);
        OutputDebugStringA(buf);
        LoadHardcodedData();
    }
    catch (const json::exception& e)
    {
        char buf[512];
        sprintf_s(buf, "★ JSONエラー: %s\n", e.what());
        OutputDebugStringA(buf);
        LoadHardcodedData();
    }
    catch (...)
    {
        OutputDebugStringW(L"★ 不明なエラー - ハードコードデータを使用\n");
        LoadHardcodedData();
    }
}

// Json読み込み失敗時のフォールバック
void EnemyDataBase::LoadHardcodedData()
{
    OutputDebugStringW(L"★ ハードコードデータを登録中...\n");

    //{
    //    EnemyData data;
    //    data.id = "slime";
    //    data.textureName = "enemy_slime";
    //    data.hp = 20;
    //    data.attack = 3;
    //    data.width = 1.0f;
    //    data.height = 1.0f;
    //    data.actions = {
    //        { EnemyActionType::Attack, 3, 1, 70, L"3ダメージ" },
    //        { EnemyActionType::Move,   0, 0, 30, L"移動" },
    //    };
    //    m_data["slime"] = data;
    //}
    //{
    //    EnemyData data;
    //    data.id = "goblin";
    //    data.textureName = "enemy_goblin";
    //    data.hp = 40;
    //    data.attack = 8;
    //    data.width = 1.0f;
    //    data.height = 1.0f;
    //    data.actions = {
    //        { EnemyActionType::Attack, 8, 1, 60, L"8ダメージ" },
    //        { EnemyActionType::Defend, 5, 0, 20, L"5ブロック" },
    //        { EnemyActionType::Move,   0, 0, 20, L"移動" },
    //    };
    //    m_data["goblin"] = data;
    //}

    OutputDebugStringW(L"★ ハードコードデータ登録完了\n");
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