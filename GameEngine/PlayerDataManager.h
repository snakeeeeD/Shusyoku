#pragma once
#include "PlayerData.h"

class PlayerDataManager
{
public:
    static void Init();           // 初期データ作成
    static void Save();           // playerdata.json に保存
    static void Load();           // playerdata.json から読み込み
    static void AddCard(const std::string& id); // カード追加

    static bool HasSaveData();
    static void DeleteSave();

    static void SaveFieldProgress(int nodeIndex, const std::vector<bool>& clearedNodes);
    static void StartNewGame();

    static PlayerData& GetData() { return m_data; }

private:
    static PlayerData m_data;
    static const char* SAVE_PATH;
};