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
	// 常に初期データで開始（コンティニューはタイトルで選択）
	m_data.hp = 50;
	m_data.maxHp = 50;
	m_data.deck = {
		"ATK_strike", "ATK_strike", "ATK_strike", "ATK_Spin Slash", "ATK_poison_blade",
		"SKL_defend", "SKL_defend",
		"MOV_move", "MOV_move", "dash",
		"POW_power_attack", "POW_buff_defense"
	};
	m_data.currentNodeIndex = 0;
	m_data.clearedNodes.clear();
	m_data.fieldPlayerCol = 0;
	m_data.fieldPlayerRow = 0;
	m_data.fieldSteps = 20;
	m_data.fieldNodeTypes.clear();
	m_data.fieldNodeEnemyIds.clear();
	m_data.fieldNodeVisited.clear();
}

void PlayerDataManager::Save()
{
	json j;
	j["hp"] = m_data.hp;
	j["maxHp"] = m_data.maxHp;
	j["deck"] = m_data.deck;
	j["currentNodeIndex"] = m_data.currentNodeIndex;
	j["clearedNodes"] = m_data.clearedNodes;

	// フィールド位置・歩数
	j["fieldPlayerCol"] = m_data.fieldPlayerCol;
	j["fieldPlayerRow"] = m_data.fieldPlayerRow;
	j["fieldSteps"] = m_data.fieldSteps;

	// フィールドノード情報
	j["fieldNodeTypes"] = m_data.fieldNodeTypes;
	j["fieldNodeEnemyIds"] = m_data.fieldNodeEnemyIds;
	j["fieldNodeVisited"] = m_data.fieldNodeVisited;

	// ← 全部追加してからファイルに書き込む
	std::ofstream file(SAVE_PATH);
	if (file.is_open())
	{
		file << j.dump(2);
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
		m_data.maxHp = j["maxHp"];
		m_data.deck = j["deck"].get<std::vector<std::string >>();

		// フィールド進行状況の読み込み
		if (j.contains("currentNodeIndex"))
			m_data.currentNodeIndex = j["currentNodeIndex"];
		else
			m_data.currentNodeIndex = 0;

		if (j.contains("clearedNodes"))
			m_data.clearedNodes = j["clearedNodes"].get<std::vector<bool>>();
		else
			m_data.clearedNodes.clear();

		m_data.fieldPlayerCol = j.value("fieldPlayerCol", 0);
		m_data.fieldPlayerRow = j.value("fieldPlayerRow", 3);
		m_data.fieldSteps = j.value("fieldSteps", 20);

		if (j.contains("fieldNodeTypes"))
			m_data.fieldNodeTypes = j["fieldNodeTypes"].get<std::vector<int>>();
		if (j.contains("fieldNodeEnemyIds"))
			m_data.fieldNodeEnemyIds = j["fieldNodeEnemyIds"].get<std::vector<std::string>>();
		if (j.contains("fieldNodeVisited"))
			m_data.fieldNodeVisited = j["fieldNodeVisited"].get<std::vector<bool>>();
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
"ATK_strike", "ATK_strike", "ATK_strike", "ATK_Spin Slash", "ATK_poison_blade",
		"SKL_defend", "SKL_defend",
		"MOV_move", "MOV_move", "dash",
		"POW_power_attack", "POW_buff_defense"
		};
	}
}

void PlayerDataManager::AddCard(const std::string& id)
{
	m_data.deck.push_back(id);
	Save();		// カード追加したら即セーブ
}

// フィールド進行を保存
void PlayerDataManager::SaveFieldProgress(int nodeIndex, const std::vector<bool>& clearedNodes)
{
	m_data.currentNodeIndex = nodeIndex;
	m_data.clearedNodes = clearedNodes;
	Save();
}

// 新規ゲーム開始
void PlayerDataManager::StartNewGame()
{
	m_data.hp = 50;
	m_data.maxHp = 50;
	m_data.deck = {
	   "ATK_strike", "ATK_strike", "ATK_strike", "ATK_Spin Slash", "ATK_poison_blade",
	   "SKL_defend", "SKL_defend", "SKL_defend", "SKL_defend",
	   "MOV_move",   "MOV_move",   "MOV_dash",
	};
	m_data.currentNodeIndex = 0;
	m_data.clearedNodes.clear();
	Save();
}

void PlayerDataManager::DeleteSave()
{
	std::remove(SAVE_PATH);
}