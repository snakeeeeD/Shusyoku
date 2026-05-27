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

	

	int GetHp() const { return m_HP; }
	int GetMaxHp() const { return m_maxHP; }

	int Think(int playerCol, int playerRow, class GridMap* gridMap);
	int GetAttack() const { return m_attack; }

	void TakeDamage(int damage);

	void DecideNextAction();                    // 次の行動を決定
	const EnemyAction* GetNextAction() const
	{
		return m_hasNextAction ? &m_nextAction : nullptr;
	}

	void MoveToward(int playerCol, int playerRow, class GridMap* gridMap);

	void AddBlock(int amount);
	void ResetBlock();
	int GetBlock() const { return m_block; }

	BuffManager& GetBuffManager() { return m_buffManager; }


private:
	bool IsAdjacentTo(int playerCol, int playerRow);

	int m_HP;
	int m_maxHP;
	int m_attack;
	int m_block;

	std::string m_textureName;
	std::string m_id;
	EnemyAction m_nextAction;
	bool m_hasNextAction;
	bool IsInRange(int targetCol, int targetRow, int range, RangeType rangeType) const;

	BuffManager m_buffManager;
};

