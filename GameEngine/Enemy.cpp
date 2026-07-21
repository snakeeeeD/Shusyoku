#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "FloatingText.h"
#include "ScreenShake.h"

Enemy::Enemy()
    : m_HP(30), m_maxHP(30)
    , m_block(0)
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
    if (!isActive) return;

    XMFLOAT4 drawColor = m_isBoss
        ? XMFLOAT4(1.0f, 0.6f, 0.6f, 1.0f)
        : GetDrawColor();

    float y = worldY;
    if (m_dying)
    {
        float t = m_deathTimer / DEATH_DUR;      // 0→1
        drawColor.w *= (1.0f - t);               // 消えていく
        y += t * 0.3f;                           // ふわっと上へ
    }

    renderer->DrawBillboard(
        TextureManager::Get(m_textureName),
        worldX, y, worldZ,
        width, height, 0.0f, drawColor);
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

    const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };

    std::vector<std::pair<float, float>> pts;

    for (int step = 0; step < steps; step++)
    {
        int curDist = abs(playerCol - gridCol) + abs(playerRow - gridRow);

        int bestCol = gridCol, bestRow = gridRow, bestDist = curDist;
        for (int d = 0; d < 4; d++)
        {
            int nc = gridCol + dirs[d][0];
            int nr = gridRow + dirs[d][1];
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;

            int nd = abs(playerCol - nc) + abs(playerRow - nr);
            if (nd > bestDist) { bestDist = nd; bestCol = nc; bestRow = nr; }   // 一番離れられる所へ
        }

        if (bestDist == curDist) break;   // どこへ動いても離れられない＝行き止まり
        gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
        gridCol = bestCol; gridRow = bestRow;
        gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);

        pts.push_back({ (gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
                        (gridRow - gridMap->GetRows() / 2.0f) * 1.1f });   // ← 通過マスを記録
    }


    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    if (!pts.empty()) StartWalk(pts, 0.1f);
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

void Enemy::TakeDamage(int damage, DamageFeel feel)
{
    // Vulnerable: 50%増
    if (m_buffManager.HasBuff(BuffType::Vulnerable))
        damage = damage * 150 / 100;

    int blocked = 0;
    if (m_block > 0)
    {
        blocked = min(m_block, damage);
        m_block -= blocked;
        damage -= blocked;
    }
    m_HP -= damage;
    if (m_HP < 0) m_HP = 0;

    if (damage > 0 && feel == DamageFeel::Hit)
    {
        StartHitFlash();
        ScreenShake::Add(ScreenShake::PowerForDamage(damage) * 0.5f);
    }
    DamageFeedback::Play(feel, worldX, worldY + height * 0.5f, worldZ, damage, blocked);
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
    if (a.select.condition.empty()) return true;
    int dist = abs(gridCol - playerCol) + abs(gridRow - playerRow);
    if (a.select.condition == "near")     return dist <= a.select.conditionValue;
    if (a.select.condition == "far")      return dist >= a.select.conditionValue;
    if (a.select.condition == "hpBelow")  return m_HP * 100 / m_maxHP <= a.select.conditionValue;
    if (a.select.condition == "turnAbove") return turn >= a.select.conditionValue;
    if (a.select.condition == "turnMultiple") return turn > 0 && a.select.conditionValue > 0 && (turn % a.select.conditionValue) == 0;
    if (a.select.condition == "turnExact")    return turn == a.select.conditionValue;
    return true;
}

void Enemy::DecideNextAction(int playerCol, int playerRow, int turn)
{
    const EnemyData* data = EnemyDataBase::Get(m_id);
    if (!data || data->actions.empty())
    {
       return;
    }

    std::vector<const EnemyAction*> cond, uncond;
    int totalC = 0, totalU = 0;
    for (auto& a : data->actions)
    {
        if (!ConditionMet(a, playerCol, playerRow, turn)) continue;
        if (a.select.condition.empty()) { uncond.push_back(&a); totalU += a.select.chance; }
        else { cond.push_back(&a);   totalC += a.select.chance; }
    }

    auto& pool = !cond.empty() ? cond : uncond;
    int   total = !cond.empty() ? totalC : totalU;

    const EnemyAction* picked = pool.empty() ? &data->actions[0] : pool.back();
    if (!pool.empty())
    {
        int roll = rand() % max(1, total);
        int cum = 0;
        for (auto* a : pool) { cum += a->select.chance; if (roll < cum) { picked = a; break; } }
    }

    m_plannedActions.clear();
    m_plannedActions.push_back(*picked);

    if (m_dmgScale != 1.0f)
        for (auto& e : m_plannedActions.back().effects)
            if (e.kind == EffectKind::Damage)
                e.value = (int)(e.value * m_dmgScale);

    m_actionIndex = 0;

    {
        int dcA = playerCol - gridCol, drA = playerRow - gridRow;
        if (abs(dcA) >= abs(drA)) { m_aimDx = (dcA > 0) ? 1 : (dcA < 0) ? -1 : 0; m_aimDy = 0; }
        else { m_aimDx = 0; m_aimDy = (drA > 0) ? 1 : (drA < 0) ? -1 : 0; }
    }

    m_actionIndex = 0;
}

int Enemy::ExecuteAction(int actionIdx, int playerCol, int playerRow,
    GridMap* gridMap, Player* player, std::vector<Enemy*>& enemies)
{
    if (actionIdx < 0 || actionIdx >= (int)m_plannedActions.size()) return 0;
    const EnemyAction& act = m_plannedActions[actionIdx];
    const TargetSpec& tg = act.target;

    // 移動より前に「当たるか」を確定させる（＝予告と一致させる）
    bool hitPlayer = tg.unavoidable
        || IsInRange(playerCol, playerRow, tg.range, tg.rangeType, tg.minRange);

    if (!tg.unavoidable)
    {
        switch (tg.approach)
        {
        case ApproachType::Toward:
            if (!hitPlayer) MoveToward(playerCol, playerRow, gridMap, tg.moveRange);  // 届かないなら詰めるだけ
            break;
        case ApproachType::Dash:
            if (!hitPlayer) hitPlayer = MoveDash(playerCol, playerRow, gridMap, tg.moveRange); // 突進は詰めて当てる
            break;
        default: break;
        }
    }

    // 効果を順に適用
    int damage = 0;
    for (auto& e : act.effects)
    {
        switch (e.kind)
        {
        case EffectKind::MoveToward: MoveToward(playerCol, playerRow, gridMap, e.value); break;
        case EffectKind::MoveAway:   MoveAway(playerCol, playerRow, gridMap, e.value);   break;

        case EffectKind::Damage:
            if (hitPlayer)
            {
                if (tg.approach != ApproachType::Dash && !tg.unavoidable && player)
                    StartLunge(player->worldX, player->worldZ);
                damage += m_buffManager.GetFinalAttack(e.value);
            }
            break;

        case EffectKind::Block:
            AddBlock(m_buffManager.GetFinalBlock(e.value));
            break;

        case EffectKind::Buff:
        case EffectKind::Debuff:
        {
            Buff b;
            b.type = StringToBuffType(e.buff);
            b.value = e.value;
            b.duration = e.duration;
            b.name = BuffInfo::Get(b.type).name;
            b.description = L"";

            if (e.applyTo == ApplyTo::Self)
                m_buffManager.AddBuff(b);
            else if (e.applyTo == ApplyTo::Player)
            {
                if (hitPlayer && player) player->GetBuffManager().AddBuff(b);
            }
            else // Allies：範囲内の他の敵
            {
                for (auto other : enemies)
                {
                    if (other == this || other->GetHp() <= 0) continue;
                    if (IsInRange(other->gridCol, other->gridRow, tg.range, tg.rangeType, tg.minRange))
                        other->GetBuffManager().AddBuff(b);
                }
            }
            break;
        }
        }
    }

    // 命中したらプリセットのエフェクトを命中点で再生
    if (hitPlayer && !act.vfx.empty() && player)
        EffectManager::Play(act.vfx, player->worldX, player->worldY + 0.5f, player->worldZ);

    return damage;
}

bool Enemy::IsThreateningCell(int col, int row, const EnemyAction& a) const
{
    const TargetSpec& tg = a.target;
    if (tg.unavoidable) return true;          // どこにいても当たる
    if (tg.approach == ApproachType::Dash)
    {
        for (int i = 1; i <= tg.moveRange; i++)
            if (gridCol + m_aimDx * i == col && gridRow + m_aimDy * i == row) return true;
        return false;
    }
    return IsInRange(col, row, tg.range, tg.rangeType, tg.minRange);
}

std::vector<std::pair<int, int>> Enemy::GetThreatCells(const EnemyAction& a, GridMap* gridMap) const
{
    std::vector<std::pair<int, int>> out;
    for (int r = 0; r < gridMap->GetRows(); r++)
        for (int c = 0; c < gridMap->GetCols(); c++)
            if (IsThreateningCell(c, r, a)) out.push_back({ c, r });
    return out;
}

void Enemy::StartDeath()
{
    if (m_dying) return;
    m_dying = true;
    m_deathTimer = 0.0f;

    // チリになる：灰色の粒を球状にまく
    EffectManager::Play("death", worldX, worldY + height * 0.5f, worldZ);
}

void Enemy::UpdateDeath(float deltaTime)
{
    if (!m_dying) return;
    m_deathTimer += deltaTime;
}

void Enemy::ApplyDifficulty(float hpMul, float dmgMul)
{
    m_maxHP = (int)(m_maxHP * hpMul);
    m_HP = m_maxHP;
    m_displayHp = (float)m_HP;
    m_dmgScale = dmgMul;
}