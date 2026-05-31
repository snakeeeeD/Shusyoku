#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"

Enemy::Enemy()
    : m_HP(30), m_maxHP(30), m_attack(5)
    , m_block(0)
    , m_hasNextAction(false)
{
    width = 1.0f;
    height = 1.0f;
    worldY = 0.5f;
}
void Enemy::Init(const std::string& id)
{
	const EnemyData* data = EnemyDataBase::Get(id);
	if (!data) return;

    m_id = id;

	m_HP = data->hp;
	m_maxHP = data->hp;
	m_attack = data->attack;
	width = data->width;
	height = data->height;
    m_isBoss = data->isBoss;
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
		worldX, worldY, worldZ + 0.5,
		width, height, 0.0f, color
	);
}

bool Enemy::IsAdjacentTo(int playerCol, int playerRow)
{
    int dc = abs(gridCol - playerCol);
    int dr = abs(gridRow - playerRow);
    return (dc + dr) == 1; // 上下左右1マス
}

bool Enemy::IsInRange(int targetCol, int targetRow, int range, RangeType rangeType) const
{
    int dc = abs(gridCol - targetCol);
    int dr = abs(gridRow - targetRow);

    switch (rangeType)
    {
    case RangeType::None:
        return false;

    case RangeType::Adjacent:
        // 隣接1マス
        return (dc + dr) == 1;

    case RangeType::Cross:
        // 十字（縦横のみ）
        if (dc == 0 && dr > 0 && dr <= range) return true;  // 縦
        if (dr == 0 && dc > 0 && dc <= range) return true;  // 横
        return false;

    case RangeType::Area:
        // 正方形範囲
        return max(dc, dr) <= range;

    case RangeType::Diamond:
        // ひし形範囲
        return (dc + dr) <= range;

    default:
        return false;
    }
}

int Enemy::Think(int playerCol, int playerRow, GridMap* gridMap)
{
    if (!m_hasNextAction) return 0;

    int dc = abs(gridCol - playerCol);
    int dr = abs(gridRow - playerRow);
    int dist = dc + dr;

    if (m_nextAction.type == EnemyActionType::Attack)
    {
        // 範囲内にいるかチェック
        if (IsInRange(playerCol, playerRow, m_nextAction.range, m_nextAction.rangeType))
        {
            // 攻撃
            return m_nextAction.value;
        }
        else
        {
            // 射程外なら近づく
            MoveToward(playerCol, playerRow, gridMap);
            return 0;
        }
    }
    else if (m_nextAction.type == EnemyActionType::Move)
    {
        MoveToward(playerCol, playerRow, gridMap);
        return 0;
    }
    else if (m_nextAction.type == EnemyActionType::Defend)
    {
        AddBlock(m_nextAction.value);
        return 0;
    }

    return 0;
}

void Enemy::MoveToward(int playerCol, int playerRow, GridMap* gridMap)
{
    int dc = playerCol - gridCol;
    int dr = playerRow - gridRow;

    std::vector<std::pair<int, int>> candidates;

    // 優先方向
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

    for (auto& [newCol, newRow] : candidates)
    {
        if (newCol < 0 || newCol >= gridMap->GetCols()) continue;
        if (newRow < 0 || newRow >= gridMap->GetRows()) continue;

        auto& cell = gridMap->GetCell(newCol, newRow);

        if (cell.type == CellType::Empty)
        {
            gridMap->SetCellType(gridCol, gridRow, CellType::Empty);

            gridCol = newCol;
            gridRow = newRow;

            gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);

            worldX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
            worldZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;

            return;
        }
    }
}

void Enemy::TakeDamage(int damage)
{
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


void Enemy::DecideNextAction()
{
    const EnemyData* data = EnemyDataBase::Get(m_id);
    if (!data || data->actions.empty())
    {
        m_hasNextAction = false;
        return;
    }

    int roll = rand() % 100;
    int cumulative = 0;
    for (auto& action : data->actions)
    {
        cumulative += action.chance;
        if (roll < cumulative)
        {
            m_nextAction = action; // コピー
            m_hasNextAction = true;
            return;
        }
    }
    m_nextAction = data->actions.back(); // コピー
    m_hasNextAction = true;
}