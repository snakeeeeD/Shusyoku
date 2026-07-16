#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"

Enemy::Enemy()
    : m_HP(30), m_maxHP(30), m_attack(5)
    , m_block(0)
    , m_hasNextAction(false)
{
    width = 1.0f;
    height = 1.0f;
    worldY = 0.05f;
}
void Enemy::Init(const std::string& id)
{
	const EnemyData* data = EnemyDataBase::Get(id);
	if (!data) return;

    m_id = id;
	m_HP = data->hp;
	m_maxHP = data->hp;
    m_displayHp = (float)m_HP;
	m_attack = data->attack;
	width = data->width;
	height = data->height;
    m_isBoss = data->isBoss;
    m_immovable = data->immovable;
	m_textureName = data->textureName;
    m_gridShape = data->gridShape;
}

void Enemy::Update(float delteTime)
{

}

void Enemy::Draw3D(Renderer3D* renderer)
{
	if (!isActive)
	{
		return;
	}

    // ボスは赤みがかった色
    XMFLOAT4 drawColor = m_isBoss
        ? XMFLOAT4(1.0f, 0.6f, 0.6f, 1.0f)
        : color;

	renderer->DrawBillboard(
		TextureManager::Get(m_textureName),
        worldX, worldY, worldZ,
		width, height, 0.0f, drawColor
	);
}

bool Enemy::IsAdjacentTo(int playerCol, int playerRow)
{
    int dc = abs(gridCol - playerCol);
    int dr = abs(gridRow - playerRow);
    return (dc + dr) == 1; // 上下左右1マス
}

bool Enemy::IsInRange(int targetCol, int targetRow, int range, RangeType rangeType, int minRange) const
{
    range += m_buffManager.GetBuffValue(BuffType::RangeUp);
    return RangeShape::Contains(gridCol, gridRow, targetCol, targetRow,
        rangeType, range, minRange, m_aimDx, m_aimDy);
}

int Enemy::ExecuteAction(int actionIdx, int playerCol, int playerRow, GridMap* gridMap, Player* player)
{
    if (actionIdx < 0 || actionIdx >= (int)m_plannedActions.size()) return 0;
    const EnemyAction& act = m_plannedActions[actionIdx];

    if (act.type == EnemyActionType::Attack)
    {
        if (act.unavoidable)
        {
            // 位置に関係なく必中
            if (!act.onHitBuffType.empty() && player)
            {
                Buff debuff;
                debuff.type = StringToBuffType(act.onHitBuffType);
                debuff.value = act.onHitValue;
                debuff.duration = act.onHitDuration;
                const auto& info = BuffInfo::Get(debuff.type);
                debuff.name = info.name;
                player->GetBuffManager().AddBuff(debuff);
            }
            return m_buffManager.GetFinalAttack(act.value);
        }


        if (IsInRange(playerCol, playerRow, act.range, act.rangeType, act.minRange))
        {
  
            if (!act.onHitBuffType.empty() && player)
            {
                Buff debuff;
                debuff.type = StringToBuffType(act.onHitBuffType);
                debuff.value = act.onHitValue;
                debuff.duration = act.onHitDuration;
                const auto& info = BuffInfo::Get(debuff.type);
                debuff.name = info.name;
                player->GetBuffManager().AddBuff(debuff);
            }
            StartLunge(player->worldX, player->worldZ);
            return m_buffManager.GetFinalAttack(act.value);
        }
        else
        {
            if (act.dash)
            {
                if (MoveDash(playerCol, playerRow, gridMap, act.moveRange))
                {
                    if (!act.onHitBuffType.empty() && player)
                    {
                        Buff debuff;
                        debuff.type = StringToBuffType(act.onHitBuffType);
                        debuff.value = act.onHitValue;
                        debuff.duration = act.onHitDuration;
                        debuff.name = BuffInfo::Get(debuff.type).name;
                        player->GetBuffManager().AddBuff(debuff);
                    }
                    return m_buffManager.GetFinalAttack(act.value);
                }
                return 0;
            }
            MoveToward(playerCol, playerRow, gridMap, act.moveRange);
            return 0;
        }
    }
    else if (act.type == EnemyActionType::Move)
    {
        MoveToward(playerCol, playerRow, gridMap, act.moveRange);
        return 0;
    }
    else if (act.type == EnemyActionType::Defend)
    {
        AddBlock(m_buffManager.GetFinalBlock(act.value));
        return 0;
    }
    else if (act.type == EnemyActionType::Buf)
    {
        Buff buff;
        buff.type = StringToBuffType(act.buffType);
        buff.value = act.value;
        buff.duration = act.duration;
        const auto& info = BuffInfo::Get(buff.type);
        buff.name = info.name;
        m_buffManager.AddBuff(buff);
        return 0;
    }
    else if (act.type == EnemyActionType::Retreat)
    {
        MoveAway(playerCol, playerRow, gridMap, act.moveRange);
        return 0;
    }
    else if (act.type == EnemyActionType::Debuf)
    {
        if (player)
        {
            Buff debuff;
            debuff.type = StringToBuffType(act.buffType);
            debuff.value = act.value;
            debuff.duration = act.duration;
            const auto& info = BuffInfo::Get(debuff.type);
            debuff.name = info.name;
            player->GetBuffManager().AddBuff(debuff);
        }
        return 0;
    }

    return 0;
}

void Enemy::MoveToward(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root))
        return;
    if (m_immovable) return;                       // 動けない敵は追わない
    if (m_buffManager.HasBuff(BuffType::Root)) return;

    for (int step = 0; step < steps; step++)
    {
        int dc = playerCol - gridCol;
        int dr = playerRow - gridRow;

        if (abs(dc) + abs(dr) <= 1) break;         // 隣接したら詰め終わり

        std::vector<std::pair<int, int>> candidates;
        if (abs(dc) >= abs(dr))
        {
            candidates.push_back({ gridCol + (dc > 0 ? 1 : -1), gridRow });
            if (dr != 0)
                candidates.push_back({ gridCol, gridRow + (dr > 0 ? 1 : -1) });
        }
        else
        {
            candidates.push_back({ gridCol, gridRow + (dr > 0 ? 1 : -1) });
            if (dc != 0)
                candidates.push_back({ gridCol + (dc > 0 ? 1 : -1), gridRow });
        }

        bool moved = false;
        for (auto& [newCol, newRow] : candidates)
        {
            if (newCol < 0 || newCol >= gridMap->GetCols()) continue;
            if (newRow < 0 || newRow >= gridMap->GetRows()) continue;
            if (gridMap->GetCell(newCol, newRow).type != CellType::Empty) continue;

            gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
            gridCol = newCol;
            gridRow = newRow;
            gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);
            moved = true;
            break;
        }
        if (!moved) break;                         // 詰まったら終了
    }

    // 最終位置へスライドアニメ（複数歩でも1回の滑らかな移動）
    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    StartMove(newX, newZ);
}

void Enemy::MoveAway(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_immovable) return;
    if (m_buffManager.HasBuff(BuffType::Root)) return;

    for (int step = 0; step < steps; step++)
    {
        int dc = playerCol - gridCol;
        int dr = playerRow - gridRow;

        // プレイヤーと逆方向へ
        std::vector<std::pair<int, int>> candidates;
        if (abs(dc) >= abs(dr))
        {
            if (dc != 0) candidates.push_back({ gridCol - (dc > 0 ? 1 : -1), gridRow });
            if (dr != 0) candidates.push_back({ gridCol, gridRow - (dr > 0 ? 1 : -1) });
        }
        else
        {
            if (dr != 0) candidates.push_back({ gridCol, gridRow - (dr > 0 ? 1 : -1) });
            if (dc != 0) candidates.push_back({ gridCol - (dc > 0 ? 1 : -1), gridRow });
        }

        bool moved = false;
        for (auto& [nc, nr] : candidates)
        {
            if (nc < 0 || nc >= gridMap->GetCols()) continue;
            if (nr < 0 || nr >= gridMap->GetRows()) continue;
            if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;

            gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
            gridCol = nc; gridRow = nr;
            gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);
            moved = true;
            break;
        }
        if (!moved) break;
    }

    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    StartMove(newX, newZ);
}

bool Enemy::MoveDash(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_immovable) return false;
    if (m_buffManager.HasBuff(BuffType::Root)) return false;
    if (m_aimDx == 0 && m_aimDy == 0) return false;

    bool hit = false;
    for (int i = 0; i < steps; i++)
    {
        int nc = gridCol + m_aimDx, nr = gridRow + m_aimDy;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        if (nc == playerCol && nr == playerRow) { hit = true; break; }      // ぶつかった＝ヒット
        if (gridMap->GetCell(nc, nr).type != CellType::Empty) break;        // 障害物で停止

        gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
        gridCol = nc; gridRow = nr;
        gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);
    }

    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    StartMove(newX, newZ);
    return hit;
}

void Enemy::TakeDamage(int damage)
{
    // Vulnerable: 50%増
    if (m_buffManager.HasBuff(BuffType::Vulnerable))
        damage = damage * 150 / 100;

    if (m_block > 0)
    {
        int blocked = min(m_block, damage);
        m_block -= blocked;
        damage -= blocked;
    }
    m_HP -= damage;
    if (m_HP < 0) m_HP = 0;
}

void Enemy::AddBlock(int amount)
{
    m_block += amount;
}

void Enemy::ResetBlock()
{
    m_block = 0;
}


bool Enemy::ConditionMet(const EnemyAction& a, int playerCol, int playerRow, int turn) const
{
    if (a.condition.empty()) return true;
    int dist = abs(gridCol - playerCol) + abs(gridRow - playerRow);
    if (a.condition == "near")     return dist <= a.conditionValue;
    if (a.condition == "far")      return dist >= a.conditionValue;
    if (a.condition == "hpBelow")  return m_HP * 100 / m_maxHP <= a.conditionValue;
    if (a.condition == "turnAbove") return turn >= a.conditionValue;
    if (a.condition == "turnMultiple") return turn > 0 && a.conditionValue > 0 && (turn % a.conditionValue) == 0;
    if (a.condition == "turnExact")    return turn == a.conditionValue;
    return true;
}

void Enemy::DecideNextAction(int playerCol, int playerRow, int turn)
{
    const EnemyData* data = EnemyDataBase::Get(m_id);
    if (!data || data->actions.empty())
    {
        m_hasNextAction = false; m_plannedActions.clear(); m_actionIndex = 0; return;
    }

    std::vector<const EnemyAction*> cond, uncond;
    int totalC = 0, totalU = 0;
    for (auto& a : data->actions)
    {
        if (!ConditionMet(a, playerCol, playerRow, turn)) continue;
        if (a.condition.empty()) { uncond.push_back(&a); totalU += a.chance; }
        else { cond.push_back(&a);   totalC += a.chance; }
    }

    auto& pool = !cond.empty() ? cond : uncond;
    int   total = !cond.empty() ? totalC : totalU;

    const EnemyAction* picked = pool.empty() ? &data->actions[0] : pool.back();
    if (!pool.empty())
    {
        int roll = rand() % max(1, total);
        int cum = 0;
        for (auto* a : pool) { cum += a->chance; if (roll < cum) { picked = a; break; } }
    }

    m_nextAction = *picked;
    m_hasNextAction = true;
    m_plannedActions.clear();
    m_plannedActions.push_back(*picked);
    for (auto& sub : picked->subActions) m_plannedActions.push_back(sub);

    {
        int dcA = playerCol - gridCol, drA = playerRow - gridRow;
        if (abs(dcA) >= abs(drA)) { m_aimDx = (dcA > 0) ? 1 : (dcA < 0) ? -1 : 0; m_aimDy = 0; }
        else { m_aimDx = 0; m_aimDy = (drA > 0) ? 1 : (drA < 0) ? -1 : 0; }
    }

    m_actionIndex = 0;
}

bool Enemy::IsThreateningCell(int col, int row, const EnemyAction& a) const
{
    if (a.dash)   // 突進＝狙う向きの直線
    {
        for (int i = 1; i <= a.moveRange; i++)
            if (gridCol + m_aimDx * i == col && gridRow + m_aimDy * i == row) return true;
        return false;
    }
    return IsInRange(col, row, a.range, a.rangeType, a.minRange);   // 形状はここが全部知ってる
}

std::vector<std::pair<int, int>> Enemy::GetThreatCells(const EnemyAction& a, GridMap* gridMap) const
{
    std::vector<std::pair<int, int>> out;
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
            if (IsThreateningCell(c, r, a)) out.push_back({ c, r });
    return out;
}