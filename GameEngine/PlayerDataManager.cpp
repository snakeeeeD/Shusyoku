#include "PlayerDataManager.h"
#include "CardDataBase.h"
#include "RelicManager.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <windows.h>
#include <algorithm>

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

	m_data.gold = 100;          // 開始所持金
	m_data.materials.clear();

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

	j["gold"] = m_data.gold;
	j["materials"] = m_data.materials;
	j["relics"] = m_data.relics;

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

		m_data.gold = j.value("gold", 0);
		if (j.contains("materials"))
			m_data.materials = j["materials"].get<std::map<std::string, int>>();
		else
			m_data.materials.clear();

		if (j.contains("relics")) m_data.relics = j["relics"].get<std::vector<std::string>>();
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

	m_data.gold = 100;          // 開始所持金
	m_data.materials.clear();
	m_data.relics.clear();

	m_data.currentNodeIndex = 0;
	m_data.clearedNodes.clear();
	m_data.fieldNodeTypes.clear();
	m_data.fieldNodeEnemyIds.clear();
	m_data.fieldNodeVisited.clear();
	Save();
}

void PlayerDataManager::DeleteSave()
{
	std::remove(SAVE_PATH);
}

void PlayerDataManager::RemoveCard(int index)
{
	if (index >= 0 && index < (int)m_data.deck.size())
	{
		m_data.deck.erase(m_data.deck.begin() + index);
		Save();
	}
}

void PlayerDataManager::AddGold(int amount)
{
	m_data.gold += amount;
	Save();
}

bool PlayerDataManager::SpendGold(int amount)
{
	if (m_data.gold < amount) return false;
	m_data.gold -= amount;
	Save();
	return true;
}

void PlayerDataManager::AddMaterial(const std::string& id, int n)
{
	m_data.materials[id] += n;
	if (m_data.materials[id] <= 0) m_data.materials.erase(id);
	Save();
}

bool PlayerDataManager::SpendMaterial(const std::string& id, int count)
{
	if (m_data.materials[id] < count) return false;
	m_data.materials[id] -= count;
	if (m_data.materials[id] <= 0) m_data.materials.erase(id);
	Save();
	return true;
}

int PlayerDataManager::GetMaterialCount(const std::string& id)
{
	auto it = m_data.materials.find(id);
	return it != m_data.materials.end() ? it->second : 0;
}

void PlayerDataManager::UpgradeCard(int index)
{
	if (index < 0 || index >= (int)m_data.deck.size()) return;
	std::string& id = m_data.deck[index];
	if (id.rfind("CRAFT:", 0) == 0) return;			  // クラフトカードはアップグレード不可
	if (!id.empty() && id.back() == '+') return;      // 既に強化済み
	if (!CardDataBase::Get(id + "+")) return;         // 強化版が無いカード
	id += "+";
	Save();
}

int PlayerDataManager::MaterialCount(const std::string& id)
{
	auto it = m_data.materials.find(id);
	return it != m_data.materials.end() ? it->second : 0;
}

void PlayerDataManager::AddRelic(const std::string& id)
{
	if (std::find(m_data.relics.begin(), m_data.relics.end(), id) == m_data.relics.end())
	{
		m_data.relics.push_back(id);

		// 取得時に効く永続ステータス系（今後もここに追記）
		if (auto d = RelicManager::Get(id))
		{
			if (d->kind == "maxHp") { m_data.maxHp += d->value; m_data.hp += d->value; }
			else if (d->kind == "steps") { m_data.fieldSteps += d->value; }
		}
		Save();
	}
}