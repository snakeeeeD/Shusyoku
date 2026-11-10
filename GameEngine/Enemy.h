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
	int GetHp() const override { return m_HP; }
	int GetMaxHp() const { return m_maxHP; }
	int GetBlock() const { return m_block; }
	int GetAimDx() const { return m_aimDx; }
	int GetAimDy() const { return m_aimDy; }
	int GetActionIndex() const { return m_actionIndex; }
	int GetLastHitDamage() const { return m_lastHitDamage; }
	int GetLastHitCount()  const { return m_lastHitCount; }
	BuffManager& GetBuffManager() { return m_buffManager; }
	const std::string& GetTextureName() const { return m_textureName; }
	void SetTexture(const std::string& t) { m_textureName = t; }
	void AddOverlay(const std::string& t) { m_overlays.push_back(t); }   // 本体の上に重ねる絵（ボスの鎖など）
	bool RemoveOverlay(const std::string& t);                            // 外せたら true
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

	void TakeDamage(int damage, DamageFeel feel = DamageFeel::Hit);

	void DecideNextAction(int playerCol, int playerRow, int turn);               // 次の行動を決定
	void SetAwakenIntent();   // 予告を「覚醒」に差し替え（覚醒ターン用）
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
	void SetInvulnerable(bool v);
	bool IsInvulnerable() const { return m_buffManager.HasBuff(BuffType::Invincible); }
	bool IsInRange(int targetCol, int targetRow, int range, RangeType rangeType, int minRange = 0) const;

	void StartDeath();
	void UpdateDeath(float deltaTime);
	bool IsDying() const { return m_dying; }
	bool IsDeathFinished() const { return m_dying && m_deathTimer >= DEATH_DUR; }

	int ExecuteAction(int actionIdx, int playerCol, int playerRow,
		GridMap* gridMap, Player* player, std::vector<Enemy*>& enemies,
		int moveTargetCol = -1, int moveTargetRow = -1, bool* didAttack = nullptr);

	bool IsCellInTarget(int col, int row, const TargetSpec& tg) const;
	bool IsThreateningCell(int col, int row, const EnemyAction& a) const;
	std::vector<std::pair<int, int>> GetThreatCells(const EnemyAction& a, class GridMap* gridMap) const;

	const std::vector<std::pair<int, int>>& GetGridShape() const { return m_gridShape; }
	void ClearFootprint(GridMap* gridMap);
	void MarkFootprint(GridMap* gridMap);
	bool CanOccupy(GridMap* gridMap, int newCol, int newRow) const;
	void CenterWorld(int col, int row, GridMap* gridMap, float& x, float& z) const;   // 形の中心のワールド座標

	void ApplyDifficulty(float hpMul, float dmgMul, int bonusActions);

	std::vector<std::pair<int, int>> PlannedMovePath(int targetCol, int targetRow, class GridMap* gridMap) const;
	std::vector<std::pair<int, int>> m_plannedMovePath;
	void SetPlannedMovePath(std::vector<std::pair<int, int>> p) { m_plannedMovePath = std::move(p); }
	const std::vector<std::pair<int, int>>& GetPlannedMovePath() const { return m_plannedMovePath; }

	bool hitsPlayer = false;   // 現在のプレイヤー位置を攻撃範囲に含む＝避けられていない
	DirectX::XMFLOAT4 hueColor{ 1, 1, 1, 1 };   // 敵ごとの識別色（マーカーのフチ用）
	int colorIndex = -1;   // 識別色の番号（出現時に決めて戦闘中は変えない）

	void SetAllyCount(int n) { m_allyCount = n; }
	void MarkJustSummoned() { m_justSummoned = true; }
	bool TakeJustSummoned() { bool v = m_justSummoned; m_justSummoned = false; return v; }
	std::vector<std::pair<std::string, int>> TakePendingSummons()
	{
		auto s = m_pendingSummons; m_pendingSummons.clear(); return s;
	}

	bool IsSnake() const { return m_isSnake; }
	const std::vector<std::pair<int, int>>& GetBodyCells() const { return m_bodyCells; }
	std::vector<std::pair<int, int>> VisualChain();   // 描画用の体の列（尾…頭）
	bool IsWeakPoint(int c, int r) const { return (c == gridCol && r == gridRow) || (c == m_weakCell.first && r == m_weakCell.second); }   // 頭＋光るウロコ
	const std::pair<int, int>& GetWeakCell() const { return m_weakCell; }
	void PickWeakCell(int playerCol, int playerRow);
	// 占有している全マス（蛇は頭＋体、多マス敵は形の全マス）
	std::vector<std::pair<int, int>> OccupiedCells() const
	{
		std::vector<std::pair<int, int>> v;
		if (IsSnake()) { v.push_back({ gridCol, gridRow }); for (auto& b : GetBodyCells()) v.push_back(b); }
		else for (auto& [dc, dr] : m_gridShape) v.push_back({ gridCol + dc, gridRow + dr });
		return v;
	}
	void InitSnake(GridMap* gridMap);
	void BindGrid(GridMap* gridMap, int cols, int rows) { m_grid = gridMap; m_gridCols = cols; m_gridRows = rows; }
	const std::vector<std::pair<int, int>>& SummonPlan() const { return m_summonPlan; }
	void ClearSummonPlan() { m_summonPlan.clear(); }
	void GrowCoil(GridMap* gridMap, int steps, class Player* player);
	void LungeTo(GridMap* gridMap);                                 // 予告した経路で首を伸ばす
	std::vector<std::pair<int, int>> LungePath(int aimCol, int aimRow) const;   // 経路を探す
	void RetractNeck(GridMap* gridMap);                             // 伸ばした首を戻す
	bool PushPlayerToCenter(GridMap* gridMap, class Player* player);
	bool OccupiesCell(int col, int row) const;
	enum class HitPart { None, Head, Body };                     // カードが当たった部位（蛇）
	void MarkHeadHit(bool h) { m_hitPart = h ? HitPart::Head : HitPart::Body; m_lastPart = m_hitPart; }
	void ReuseHitPart() { m_hitPart = m_lastPart; }              // 連撃の2発目以降も同じ部位
	void MarkWeakHit(int c, int r) { m_hitWeak = (c == m_weakCell.first && r == m_weakCell.second); }   // 光るウロコに当たったか
	bool ConsumeShed() { bool b = m_shedPending; m_shedPending = false; return b; }   // 脱皮した瞬間か（演出用）
	static constexpr int SHED_DAMAGE = 120;   // これだけカードで殴られると脱皮
	void ClearHitMark() { m_hitPart = HitPart::None; m_hitWeak = false; }   // 部位の印を消す（罠・起爆は部位に関係なし・脱皮にも数えない）
	void MarkHitCell(int c, int r) { m_hitCell = { c, r }; }      // カードが当たったマス
	const std::pair<int, int>& HitCell() const { return m_hitCell; }
	int ShedCount() const { return m_coilBonus / 2; }             // 脱皮した回数

	struct PendingCurse { std::string cardId, target; int count; };
	std::vector<PendingCurse>& PendingCurses() { return m_pendingCurses; }
	std::vector<Buff>& PendingPlayerBuffs() { return m_pendingPlayerBuffs; }

	// 範囲の中心マス（演出の着弾点に使う）。Self の時は渡した既定値を返す
	void GetAnchorCell(const EnemyAction& a, int defC, int defR, int& outC, int& outR) const
	{
		const TargetSpec& tg = a.target;
		if (tg.anchor == AnchorType::Player) { outC = m_lockC;      outR = m_lockR; }
		else if (tg.anchor == AnchorType::Fixed) { outC = tg.anchorCol; outR = tg.anchorRow; }
		else { outC = defC;         outR = defR; }
	}

	void FaceToward(float targetX);                 // プレイヤーの方を向く（画像は左向きが基準）
	XMFLOAT4 FacingUV() const { return m_faceRight ? XMFLOAT4(1, 0, -1, 1) : XMFLOAT4(0, 0, 1, 1); }

private:
	bool IsAdjacentTo(int playerCol, int playerRow);

	int m_HP;
	int m_maxHP;
	int m_block;
	bool m_isBoss;

	float m_drawZ = 0.4f;

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
	int m_lockC = 0, m_lockR = 0;   // Playerアンカー：行動決定時のプレイヤー位置

	int m_lastHitDamage = 0;
	int m_lastHitCount = 1;

	std::string m_textureName;
	std::vector<std::string> m_overlays;
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
	std::vector<int> m_actionUses;   // 行動ごとの使用回数（select.maxUses の判定用）
	bool m_justSummoned = false;
	std::vector<std::pair<std::string, int>> m_pendingSummons;



	bool m_isSnake = false;
	std::vector<std::pair<int, int>> m_bodyCells;   // 胴体（通った跡）
	std::pair<int, int> m_weakCell{ -1, -1 };       // 光るウロコ（殴ると頭と同じ2倍）
	bool m_hitWeak = false;
	std::pair<int, int> m_hitCell{ -1, -1 };
	int m_cardDamage = 0;                           // カードの攻撃で受けた累計（脱皮の判定）
	int m_coilBonus = 0;                            // 脱皮で増えた巻きつきの速さ
	bool m_shedPending = false;
	int m_neckLen = 0;                              // 伸ばしている首のマス数
	std::vector<std::pair<int, int>> m_lungePlan;   // 噛みつきの経路（予告の時に決める）
	std::vector<std::pair<int, int>> m_summonPlan;   // 生成位置（予告の時に決める）
	void PlanSummon(const EnemyAction& a);
	struct SnakeOp { bool push; std::pair<int, int> cell; };   // 1マス分の見た目の操作
	std::vector<SnakeOp> m_snakeOps;
	int m_opApplied = 0;
	std::vector<std::pair<int, int>> m_visualBase;
	void QueueSnakeOp(bool push, std::pair<int, int> cell);
	GridMap* m_grid = nullptr;                      // 経路探し用（InitSnakeで入れる）
	int m_turnsSinceLunge = 0;                      // 前の噛みつきからのターン数
	int m_gridCols = 0, m_gridRows = 0;             // 盤面の大きさ（渦の内側を数える）
	std::vector<std::pair<int, int>> m_spiral;      // 頭が辿る渦巻き
	int  m_spiralIdx = 0;
	bool m_coilComplete = false;
	HitPart m_hitPart = HitPart::None;
	HitPart m_lastPart = HitPart::None;
	bool m_coilReached = false;   // 中央（距離1以内）に到達したか
	int  m_coilStuck = 0;       // 到達後の経過ターン（フィニッシャー遅延）

	std::vector<PendingCurse> m_pendingCurses;
	std::vector<Buff> m_pendingPlayerBuffs;   // ダメージ適用後に付与する状態異常

	bool m_faceRight = false;                       // true＝右向き（左向きの画像を左右反転）
};

