#pragma once
#include "GameObject.h"
#include "GridMap.h"
#include "Enemydata.h"
#include "EnemyDataBase.h"
#include "EnemyAction.h"
#include "BuffManager.h"

// < --- Enemyクラス --- >(仮で作成、ステータスは適当)
class Enemy : public GameObject
{
public:
	Enemy();
	void Init(const std::string& id);
	void Update(float deltaTime) override;
	void Draw3D(class Renderer3D* renderer) override;

	
	// ゲッター
	int GetHp() const { return m_HP; }
	int GetMaxHp() const { return m_maxHP; }
	int GetAttack() const { return m_attack; }
	int GetBlock() const { return m_block; }
	float GetDisplayHp() const { return m_displayHp; }
	BuffManager& GetBuffManager() { return m_buffManager; }
	const std::string& GetTextureName() const { return m_textureName; }
	const std::string& GetId() const { return m_id; }

	// セッター
	void SetHp(int hp) { m_HP = hp; }

	int Think(int playerCol, int playerRow, class GridMap* gridMap, class Player* player);

	void UpdateDisplayHp(float deltaTime) {
		float speed = 0.5f;
		if (m_displayHp > (float)GetHp())
			m_displayHp -= speed * deltaTime * (m_displayHp - GetHp());
		if (m_displayHp < (float)GetHp())
			m_displayHp = (float)GetHp();
	}

	void TakeDamage(int damage);

	void DecideNextAction();                    // 次の行動を決定
	const EnemyAction* GetNextAction() const
	{
		return m_hasNextAction ? &m_nextAction : nullptr;
	}

	void MoveToward(int playerCol, int playerRow, class GridMap* gridMap);

	void AddBlock(int amount);
	void ResetBlock();

	bool IsBoss() const { return m_isBoss; }

	const std::vector<std::pair<int, int>>& GetGridShape() const { return m_gridShape; }

private:
	bool IsAdjacentTo(int playerCol, int playerRow);

	int m_HP;
	int m_maxHP;
	int m_attack;
	int m_block;
	bool m_isBoss;
	float m_displayHp;

	std::string m_textureName;
	std::string m_id;
	EnemyAction m_nextAction;
	bool m_hasNextAction;
	bool IsInRange(int targetCol, int targetRow, int range, RangeType rangeType) const;

	BuffManager m_buffManager;

	std::vector<std::pair<int, int>> m_gridShape;
};

