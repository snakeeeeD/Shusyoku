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
	int GetActionIndex() const { return m_actionIndex; }
	int GetLastHitDamage() const { return m_lastHitDamage; }
	int GetLastHitCount()  const { return m_lastHitCount; }
	float GetDisplayHp() const { return m_displayHp; }
	BuffManager& GetBuffManager() { return m_buffManager; }
	const std::string& GetTextureName() const { return m_textureName; }
	const std::string& GetId() const { return m_id; }
	const std::vector<std::pair<int, int>>& GetMovePath() const { return m_movePath; }

	bool DidDash() const { return m_didDash; }

	// セッター
	void SetHp(int hp) { m_HP = hp; }
	void SetActionIndex(int i) { m_actionIndex = i; }

	bool HasMoreActions() const { return m_actionIndex < (int)m_plannedActions.size(); }

	bool IsActionUnavoidable(int idx) const {
		return idx >= 0 && idx < (int)m_plannedActions.size()
			&& m_plannedActions[idx].target.unavoidable;
	}

	void UpdateDisplayHp(float deltaTime) {
		float speed = 0.5f;        // 比例分（差が大きいほど速い）
		float minSpeed = 15.0f;    // 最低速度(HP/秒) → 遅延を防ぐ
		if (m_displayHp > (float)GetHp())
		{
			float rate = speed * (m_displayHp - GetHp());
			if (rate < minSpeed) rate = minSpeed;
			m_displayHp -= rate * deltaTime;
			if (m_displayHp < (float)GetHp()) m_displayHp = (float)GetHp();
		}
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
	void MoveAlongPlanned(GridMap* gridMap);   // テレグラフ経路(m_plannedMovePath)どおりに動く
	void AlignToTarget(int playerCol, int playerRow, GridMap* gridMap, int steps);

	void MoveToCenter(GridMap* gridMap, int steps);

	void AddBlock(int amount);
	void ResetBlock();

	void PullPlayer(int playerCol, int playerRow, class GridMap* gridMap, class Player* player, int steps);
	void KnockbackPlayer(int playerCol, int playerRow, class GridMap* gridMap, class Player* player, int steps);

	bool IsBoss() const { return m_isBoss; }
	bool IsImmovable() const { return m_immovable; }
	bool IsInRange(int targetCol, int targetRow, int range, RangeType rangeType, int minRange = 0) const;

	void StartDeath();
	void UpdateDeath(float deltaTime);
	bool IsDying() const { return m_dying; }
	bool IsDeathFinished() const { return m_dying && m_deathTimer >= DEATH_DUR; }

	int ExecuteAction(int actionIdx, int playerCol, int playerRow,
		GridMap* gridMap, Player* player, std::vector<Enemy*>& enemies,
		int moveTargetCol = -1, int moveTargetRow = -1, bool* didAttack = nullptr);

	bool IsThreateningCell(int col, int row, const EnemyAction& a) const;
	std::vector<std::pair<int, int>> GetThreatCells(const EnemyAction& a, class GridMap* gridMap) const;

	const std::vector<std::pair<int, int>>& GetGridShape() const { return m_gridShape; }

	void ApplyDifficulty(float hpMul, float dmgMul, int bonusActions);

	std::vector<std::pair<int, int>> PlannedMovePath(int targetCol, int targetRow, class GridMap* gridMap) const;
	std::vector<std::pair<int, int>> m_plannedMovePath;
	void SetPlannedMovePath(std::vector<std::pair<int, int>> p) { m_plannedMovePath = std::move(p); }
	const std::vector<std::pair<int, int>>& GetPlannedMovePath() const { return m_plannedMovePath; }

	bool hitsPlayer = false;   // 現在のプレイヤー位置を攻撃範囲に含む＝避けられていない
	DirectX::XMFLOAT4 hueColor{ 1, 1, 1, 1 };   // 敵ごとの識別色（マーカーのフチ用）

	void SetAllyCount(int n) { m_allyCount = n; }
	void MarkJustSummoned() { m_justSummoned = true; }
	bool TakeJustSummoned() { bool v = m_justSummoned; m_justSummoned = false; return v; }
	std::vector<std::pair<std::string, int>> TakePendingSummons()
	{
		auto s = m_pendingSummons; m_pendingSummons.clear(); return s;
	}

	bool IsSnake() const { return m_isSnake; }
	const std::vector<std::pair<int, int>>& GetBodyCells() const { return m_bodyCells; }
	void InitSnake(GridMap* gridMap);
	void GrowCoil(GridMap* gridMap, int steps, class Player* player);
	bool PushPlayerToCenter(GridMap* gridMap, class Player* player);
	bool OccupiesCell(int col, int row) const;
	void MarkHeadHit(bool h) { m_lastHitHead = h; }

	struct PendingCurse { std::string cardId, target; int count; };
	std::vector<PendingCurse>& PendingCurses() { return m_pendingCurses; }

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
	int   m_bonusActions = 0;
	int	  m_seqIndex = 0; 
	bool m_lastAttackWhiffed = false;   // 直前の攻撃が避けられた(空振り)か → afterDodge条件で使う
	int m_missStreak = 0;   // 連続で攻撃を外した回数（ブロックのみのターンは変化しない）

	int m_idleTurns = 0;
	int m_lastDecideCol = -999, m_lastDecideRow = -999;

	int m_lastHitDamage = 0;
	int m_lastHitCount = 1;

	std::string m_textureName;
	std::string m_id;
	std::vector<EnemyAction> m_plannedActions;   // このターンの実行プラン（メイン＋サブ）
	int m_actionIndex = 0;

	bool m_immovable = false;

	bool ConditionMet(const EnemyAction& a, int playerCol, int playerRow, int turn) const;

	BuffManager m_buffManager;

	std::vector<std::pair<int, int>> m_gridShape;

	std::vector<std::pair<int, int>> m_movePath;   // このアクションで通過したマス（罠判定＋ダッシュのグライド用）
	bool m_didDash = false;                        // 直近の移動がダッシュか（グライド演出の判別）

	void DropTrail(GridMap* gridMap, int col, int row);   // EmberTrail中は通ったマスに炎

	int  m_allyCount = 0;
	bool m_justSummoned = false;
	std::vector<std::pair<std::string, int>> m_pendingSummons;

	void ClearFootprint(GridMap* gridMap);
	void MarkFootprint(GridMap* gridMap);
	bool CanOccupy(GridMap* gridMap, int newCol, int newRow) const;


	bool m_isSnake = false;
	std::vector<std::pair<int, int>> m_bodyCells;   // 胴体（通った跡）
	std::vector<std::pair<int, int>> m_spiral;      // 頭が辿る渦巻き
	int  m_spiralIdx = 0;
	bool m_coilComplete = false;
	bool m_lastHitHead = false;
	bool m_coilReached = false;   // 中央（距離1以内）に到達したか
	int  m_coilStuck = 0;       // 到達後の経過ターン（フィニッシャー遅延）

	std::vector<PendingCurse> m_pendingCurses;
};

