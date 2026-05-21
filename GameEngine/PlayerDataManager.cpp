#include "PlayerDataManager.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <windows.h>

using json = nlohmann::json;

PlayerData PlayerDataManager::m_data;
const char* PlayerDataManager::SAVE_PATH = "Assets/Data/playerdata.json";

bool PlayerDataManager::HasSaveData()
{
	std::ifstream file(SAVE_PATH);
	return file.is_open();
}

void PlayerDataManager::Init()
{
	if (HasSaveData())
	{
		Load();
		OutputDebugStringW(L" セーブデータ読み込み\n");
	}
	else
	{
		// 初期データ
		m_data.hp = 50;
		m_data.maxHp = 50;
		m_data.deck =
		{
			"strike", "strike", "strike", "Spin Slash",
			"defend", "defend",
			"move", "move", "move"
		};
		OutputDebugStringW(L" 初期データ作成\N");
	}
}

void PlayerDataManager::Save()
{
	json j;
	j["hp"] = m_data.hp;
	j["maxHp"] = m_data.maxHp;
	j["deck"] = m_data.deck;

	std::ofstream file(SAVE_PATH);
	if (file.is_open())
	{
		file << j.dump(2); // 見やすく整形して保存
		OutputDebugStringW(L"★ セーブ完了\n");
	}
	else
	{
		OutputDebugStringW(L"★ セーブ失敗\n");
	}
}



void PlayerDataManager::Load()
{
	std::ifstream file(SAVE_PATH);
	if (!file.is_open()) return;

	try
	{
		json j;
		file >> j;

		m_data.hp = j["hp"];
		m_data.maxHp = j["maxhp"];
		m_data.deck = j["deck"].get<std::vector<std::string >>();
	}
	catch (const json::exception& e)
	{
		char buf[512];
		sprintf_s(buf, " セーブデータ読み込みエラー: %s\n", e.what());
		OutputDebugStringA(buf);

		// 失敗したら初期データ
		m_data.hp = 50;
		m_data.maxHp = 50;
		m_data.deck = {
	   "strike", "strike", "strike", "Spin Slash", "poison_blade",
	   "defend", "defend",
	   "move",   "move",   "dash",
	   "power_attack", "buff_defense"
		};
	}
}

void PlayerDataManager::AddCard(const std::string& id)
{
	m_data.deck.push_back(id);
	Save();		// カード追加したら即セーブ
}