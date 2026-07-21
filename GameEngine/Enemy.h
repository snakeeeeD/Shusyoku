#pragma once
#include "GameObject.h"
#include "GridMap.h"
#include "Enemydata.h"
#include "EnemyDataBase.h"
#include "EnemyAction.h"
#include "BuffManager.h"
#include "DamageFeedback.h"

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
	int GetBlock() const { return m_block; }
	int GetAimDx() const { return m_aimDx; }
	int GetAimDy() const { return m_aimDy; }
	float GetDisplayHp() const { return m_displayHp; }
	BuffManager& GetBuffManager() { return m_buffManager; }
	const std::string& GetTextureName() const { return m_textureName; }
	const std::string& GetId() const { return m_id; }

	// セッター
	void SetHp(int hp) { m_HP = hp; }

	int GetActionIndex() const { return m_actionIndex; }
	void SetActionIndex(int i) { m_actionIndex = i; }
	bool HasMoreActions() const { return m_actionIndex < (int)m_plannedActions.size(); }

	void UpdateDisplayHp(float deltaTime) {
		float speed = 0.5f;
		if (m_displayHp > (float)GetHp())
			m_displayHp -= speed * deltaTime * (m_displayHp - GetHp());
		if (m_displayHp < (float)GetHp())
			m_displayHp = (float)GetHp();
	}

	void TakeDamage(int damage, DamageFeel feel = DamageFeel::Hit);

	void DecideNextAction(int playerCol, int playerRow, int turn);               // 次の行動を決定
	const EnemyAction* GetNextAction() const
	{
		return m_plannedActions.empty() ? nullptr : &m_plannedActions[0];
	}
	const std::vector<EnemyAction>& GetPlannedActions() const { return m_plannedActions; }

	void MoveToward(int playerCol, int playerRow, class GridMap* gridMap, int steps = 1);
	void MoveAway(int playerCol, int playerRow, class GridMap* gridMap, int steps = 1);
	bool MoveDash(int playerCol, int playerRow, class GridMap* gridMap, int steps = 1);

	void AddBlock(int amount);
	void ResetBlock();

	bool IsBoss() const { return m_isBoss; }
	bool IsImmovable() const { return m_immovable; }
	bool IsInRange(int targetCol, int targetRow, int range, RangeType rangeType, int minRange = 0) const;

	void StartDeath();
	void UpdateDeath(float deltaTime);
	bool IsDying() const { return m_dying; }
	bool IsDeathFinished() const { return m_dying && m_deathTimer >= DEATH_DUR; }

	int ExecuteAction(int actionIdx, int playerCol, int playerRow,
		class GridMap* gridMap, class Player* player,
		std::vector<Enemy*>& enemies);

	bool IsThreateningCell(int col, int row, const EnemyAction& a) const;
	std::vector<std::pair<int, int>> GetThreatCells(const EnemyAction& a, class GridMap* gridMap) const;

	const std::vector<std::pair<int, int>>& GetGridShape() const { return m_gridShape; }

	void ApplyDifficulty(float hpMul, float dmgMul);

private:
	bool IsAdjacentTo(int playerCol, int playerRow);

	int m_HP;
	int m_maxHP;
	int m_block;
	bool m_isBoss;
	float m_displayHp;

	int m_aimDx = 0, m_aimDy = 0;

	bool  m_dying = false;
	float m_deathTimer = 0.0f;
	static constexpr float DEATH_DUR = 0.6f;

	float m_dmgScale = 1.0f;

	std::string m_textureName;
	std::string m_id;
	std::vector<EnemyAction> m_plannedActions;   // このターンの実行プラン（メイン＋サブ）
	int m_actionIndex = 0;

	bool m_immovable = false;

	bool ConditionMet(const EnemyAction& a, int playerCol, int playerRow, int turn) const;

	BuffManager m_buffManager;

	std::vector<std::pair<int, int>> m_gridShape;
};

