#include "Enemy.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "TerrainDataBase.h"
#include "Player.h"
#include "BuffInfo.h"
#include "GameUtils.h"
#include "RangeShape.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Audio.h"

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
    m_isSnake = data->snake;
}

void Enemy::Update(float delteTime)
{

}

void Enemy::Draw3D(Renderer3D* renderer)
{
    if (!isActive) return;
    if (m_isSnake) return;

    XMFLOAT4 drawColor = m_isBoss
        ? XMFLOAT4(1.0f, 0.6f, 0.6f, 1.0f)
        : GetDrawColor();

    float y = worldY;
    float s = GetJumpScale();
    if (m_dying)
    {
        float t = m_deathTimer / DEATH_DUR;
        drawColor.w *= (1.0f - t);
        y += t * 0.3f;
    }

    renderer->DrawBillboard(
        TextureManager::Get(m_textureName),
        worldX, y, worldZ,
        width * s, height * s, 0.0f, drawColor);
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
    // 多マス敵は占有マスすべてから判定（十字/直線などが敵の幅に広がる）
    for (auto& [dc, dr] : GetGridShape())
        if (RangeShape::Contains(gridCol + dc, gridRow + dr, targetCol, targetRow,
            rangeType, range, minRange, m_aimDx, m_aimDy))
            return true;
    return false;
}

void Enemy::ClearFootprint(GridMap* gridMap)
{
    for (auto& [dc, dr] : GetGridShape())
        gridMap->SetCellType(gridCol + dc, gridRow + dr, CellType::Empty);
}
void Enemy::MarkFootprint(GridMap* gridMap)
{
    CellType t = m_isBoss ? CellType::Boss : CellType::Enemy;
    for (auto& [dc, dr] : GetGridShape())
        gridMap->SetCellType(gridCol + dc, gridRow + dr, t);
}
bool Enemy::CanOccupy(GridMap* gridMap, int nc, int nr) const
{
    for (auto& [dc, dr] : GetGridShape())
    {
        int c = nc + dc, r = nr + dr;
        if (c < 0 || c >= gridMap->GetCols() || r < 0 || r >= gridMap->GetRows()) return false;
        if (gridMap->GetCell(c, r).type == CellType::Empty) continue;
        bool own = false;                              // 自分の現footprintは重なってOK
        for (auto& [odc, odr] : GetGridShape())
            if (c == gridCol + odc && r == gridRow + odr) { own = true; break; }
        if (!own) return false;                        // プレイヤー/他敵/壁はNG
    }
    return true;
}

void Enemy::MoveToward(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root))
        return;

    size_t pathStart = m_movePath.size();

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
            if (!CanOccupy(gridMap, newCol, newRow)) continue;

            ClearFootprint(gridMap);
            DropTrail(gridMap, gridCol, gridRow);      // ← 通ったマスに炎
            gridCol = newCol;
            gridRow = newRow;
            MarkFootprint(gridMap);
            m_movePath.push_back({ gridCol, gridRow });
            moved = true;
            break;
        }
        if (!moved) break;                         // 詰まったら終了
    }

    // 実際に動いた時だけスライド（0マス移動でStartMoveすると無反応な待ちが出るため）
    if (m_movePath.size() != pathStart)
        StartMove((gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
            (gridRow - gridMap->GetRows() / 2.0f) * 1.1f);
}

void Enemy::DropTrail(GridMap* gridMap, int col, int row)
{
    if (!m_buffManager.HasBuff(BuffType::EmberTrail)) return;
    auto& cell = gridMap->GetCell(col, row);
    if (cell.tileEffect.active) return;          // 既存の床は上書きしない
    cell.tileEffect.active = true;
    cell.tileEffect.id = "fire";
    cell.tileEffect.enemyOwned = true;
    cell.tileEffect.value = 2;
    cell.tileEffect.duration = 3;
    const TerrainDef* t = TerrainDataBase::Get("fire");
    if (t) cell.tileEffect.persistent = t->persistent;
}

void Enemy::MoveAway(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
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
            if (!CanOccupy(gridMap, nc, nr)) continue;

            // 移動確定部：
            ClearFootprint(gridMap);
            gridCol = bestCol; gridRow = bestRow;
            MarkFootprint(gridMap);

            int nd = abs(playerCol - nc) + abs(playerRow - nr);
            if (nd > bestDist) { bestDist = nd; bestCol = nc; bestRow = nr; }   // 一番離れられる所へ
        }

        if (bestDist == curDist) break;   // どこへ動いても離れられない＝行き止まり
        gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
        gridCol = bestCol; gridRow = bestRow;
        gridMap->SetCellType(gridCol, gridRow, CellType::Enemy);
        m_movePath.push_back({ gridCol, gridRow });

        pts.push_back({ (gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
                        (gridRow - gridMap->GetRows() / 2.0f) * 1.1f });   // ← 通過マスを記録
    }


    float newX = (gridCol - gridMap->GetCols() / 2.0f) * 1.1f;
    float newZ = (gridRow - gridMap->GetRows() / 2.0f) * 1.1f;
    if (!pts.empty()) StartWalk(pts, 0.1f);
}

bool Enemy::MoveDash(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return false;
    if (m_aimDx == 0 && m_aimDy == 0) return false;

    bool hit = false;
    for (int i = 0; i < steps; i++)
    {
        int nc = gridCol + m_aimDx, nr = gridRow + m_aimDy;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        if (nc == playerCol && nr == playerRow) { hit = true; break; }      // ぶつかった＝ヒット
        if (!CanOccupy(gridMap, nc, nr)) break;

        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
        m_didDash = true;                        // ダッシュで実際に進んだ
    }

    return hit;
}

void Enemy::MoveAlongPlanned(GridMap* gridMap)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;

    size_t pathStart = m_movePath.size();

    for (auto& cell : m_plannedMovePath)
    {
        int nc = cell.first, nr = cell.second;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        if (!CanOccupy(gridMap, nc, nr)) break;  // 途中が埋まってたら手前で止まる
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
    }
    if (m_movePath.size() != pathStart)
        StartMove((gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
            (gridRow - gridMap->GetRows() / 2.0f) * 1.1f);
}

void Enemy::AlignToTarget(int playerCol, int playerRow, GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;
    size_t pathStart = m_movePath.size();
    for (int s = 0; s < steps; s++)
    {
        int dc = playerCol - gridCol, dr = playerRow - gridRow;
        if (dc == 0 || dr == 0) break;                    // 既に十字線上
        int mc = 0, mr = 0;
        if (abs(dc) <= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 小さい方の軸を詰める＝距離は保つ
        else                    mr = (dr > 0) ? 1 : -1;
        int nc = gridCol + mc, nr = gridRow + mr;
        if (!CanOccupy(gridMap, nc, nr)) break;           // 障害物で停止
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });        // 通過罠判定用
    }
    if (m_movePath.size() != pathStart)
        StartMove((gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
            (gridRow - gridMap->GetRows() / 2.0f) * 1.1f);
}

void Enemy::MoveToCenter(GridMap* gridMap, int steps)
{
    if (m_buffManager.HasBuff(BuffType::Root)) return;
    int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
    size_t pathStart = m_movePath.size();
    for (int s = 0; s < steps; s++)
    {
        int dc = cc - gridCol, dr = cr - gridRow;
        if (dc == 0 && dr == 0) break;                    // 中央到達
        int mc = 0, mr = 0;
        if (abs(dc) >= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 大きい方の軸から詰める
        else                    mr = (dr > 0) ? 1 : -1;
        int nc = gridCol + mc, nr = gridRow + mr;
        if (!CanOccupy(gridMap, nc, nr)) break;
        ClearFootprint(gridMap);
        gridCol = nc; gridRow = nr;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });
    }
    if (m_movePath.size() != pathStart)
        StartMove((gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
            (gridRow - gridMap->GetRows() / 2.0f) * 1.1f);
}

void Enemy::TakeDamage(int damage, DamageFeel feel)
{
    Audio::PlaySE("Assets/Sound/se/hit.mp3");

    if (m_isSnake && m_lastHitHead) damage *= 2;   // 頭は弱点（2倍）
    m_lastHitHead = false;

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

        HitStop::Add(0.05f);
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

void Enemy::PullPlayer(int playerCol, int playerRow, GridMap* gridMap, Player* player, int steps)
{
    if (!player) return;
    int pc = playerCol, pr = playerRow;
    std::vector<std::pair<float, float>> pts;

    for (int step = 0; step < steps; step++)
    {
        int dc = gridCol - pc;      // 敵の方向へ
        int dr = gridRow - pr;
        if (abs(dc) + abs(dr) <= 1) break;   // 敵に隣接したら止める

        std::vector<std::pair<int, int>> candidates;
        if (abs(dc) >= abs(dr))
        {
            candidates.push_back({ pc + (dc > 0 ? 1 : -1), pr });
            if (dr != 0) candidates.push_back({ pc, pr + (dr > 0 ? 1 : -1) });
        }
        else
        {
            candidates.push_back({ pc, pr + (dr > 0 ? 1 : -1) });
            if (dc != 0) candidates.push_back({ pc + (dc > 0 ? 1 : -1), pr });
        }

        bool moved = false;
        for (auto& [nc, nr] : candidates)
        {
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;   // 壁・敵で止まる
            gridMap->SetCellType(pc, pr, CellType::Empty);
            pc = nc; pr = nr;
            gridMap->SetCellType(pc, pr, CellType::Player);
            pts.push_back({ (pc - gridMap->GetCols() / 2.0f) * 1.1f,
                            (pr - gridMap->GetRows() / 2.0f) * 1.1f });
            moved = true;
            break;
        }
        if (!moved) break;
    }

    player->gridCol = pc;
    player->gridRow = pr;
    if (!pts.empty()) player->StartWalk(pts, 0.1f);
}

void Enemy::KnockbackPlayer(int playerCol, int playerRow, GridMap* gridMap, Player* player, int steps)
{
    if (!player) return;
    int pc = playerCol, pr = playerRow;

    // 敵から離れる主方向を1軸に固定（まっすぐ吹き飛ばす）
    int dc = pc - gridCol, dr = pr - gridRow;
    if (dc == 0 && dr == 0) return;
    int stepCol = 0, stepRow = 0;
    if (abs(dc) >= abs(dr)) stepCol = (dc >= 0) ? 1 : -1;
    else                    stepRow = (dr >= 0) ? 1 : -1;

    std::vector<std::pair<float, float>> pts;
    for (int step = 0; step < steps; step++)
    {
        int nc = pc + stepCol, nr = pr + stepRow;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
        if (gridMap->GetCell(nc, nr).type != CellType::Empty) break;   // 壁・敵で止まる
        gridMap->SetCellType(pc, pr, CellType::Empty);
        pc = nc; pr = nr;
        gridMap->SetCellType(pc, pr, CellType::Player);
        pts.push_back({ (pc - gridMap->GetCols() / 2.0f) * 1.1f,
                        (pr - gridMap->GetRows() / 2.0f) * 1.1f });
    }

    player->gridCol = pc;
    player->gridRow = pr;
    if (!pts.empty()) player->StartWalk(pts, 0.08f);   // 速めで吹き飛ばす感
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
    if (a.select.condition == "afterDodge") return m_lastAttackWhiffed;
    if (a.select.condition == "allyBelow") return m_allyCount < a.select.conditionValue;
    if (a.select.condition == "coiled") return m_coilComplete;
    return true;
}

void Enemy::DecideNextAction(int playerCol, int playerRow, int turn)
{
    const EnemyData* data = EnemyDataBase::Get(m_id);
    if (!data || data->actions.empty())
    {
       return;
    }

    // 前回の決定時から動いたか（棒立ち検知）
    if (gridCol == m_lastDecideCol && gridRow == m_lastDecideRow) m_idleTurns++;
    else m_idleTurns = 0;
    m_lastDecideCol = gridCol; m_lastDecideRow = gridRow;

    // 順番モード：行動を定義順に1つずつ回す
    if (data->sequential)
    {
        // 条件付き行動が満たされていれば最優先
        const EnemyAction* picked = nullptr;
        for (auto& a : data->actions)
            if (!a.select.condition.empty() && ConditionMet(a, playerCol, playerRow, turn))
            {
                picked = &a; break;
            }

        if (!picked)
        {
            // 無条件行動だけを定義順に回す
            std::vector<const EnemyAction*> seq;
            for (auto& a : data->actions)
                if (a.select.condition.empty()) seq.push_back(&a);
            if (!seq.empty()) { picked = seq[m_seqIndex % (int)seq.size()]; m_seqIndex++; }
            else picked = &data->actions[0];
        }

        m_plannedActions.clear();
        m_plannedActions.push_back(*picked);
        if (m_dmgScale != 1.0f)
            for (auto& e : m_plannedActions.back().effects)
                if (e.kind == EffectKind::Damage)
                    e.value = (int)(e.value * m_dmgScale);
        m_actionIndex = 0;

        int dcA = playerCol - gridCol, drA = playerRow - gridRow;
        if (abs(dcA) >= abs(drA)) { m_aimDx = (dcA > 0) ? 1 : (dcA < 0) ? -1 : 0; m_aimDy = 0; }
        else { m_aimDx = 0; m_aimDy = (drA > 0) ? 1 : (drA < 0) ? -1 : 0; }

        // 2ターン動いてなければ「移動」を足す。
      // 別行動にすると予告(説明↔アイコン)がちぐはぐになるので、既存の行動に効果として足す
        if (m_idleTurns >= 2 && !m_plannedActions.empty())
        {
            auto& act = m_plannedActions[0];
            bool alreadyMoves = (act.target.approach != ApproachType::None);
            for (auto& e : act.effects)
                if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway) alreadyMoves = true;

            if (!alreadyMoves)
            {
                Effect e; e.kind = EffectKind::MoveToward; e.value = 2;
                act.effects.push_back(e);   // 元の行動に移動を追加（説明はそのまま＋移動アイコンが並ぶ）
                act.description += L"＋移動";
            }
            m_idleTurns = 0;
        }

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

    m_plannedActions.clear();
    int count = 1 + m_bonusActions;
    for (int n = 0; n < count; n++)
    {
        const EnemyAction* picked = pool.empty() ? &data->actions[0] : pool.back();
        if (!pool.empty())
        {
            int roll = rand() % max(1, total);
            int cum = 0;
            for (auto* a : pool) { cum += a->select.chance; if (roll < cum) { picked = a; break; } }
        }
        m_plannedActions.push_back(*picked);
        if (m_dmgScale != 1.0f)
            for (auto& e : m_plannedActions.back().effects)
                if (e.kind == EffectKind::Damage)
                    e.value = (int)(e.value * m_dmgScale);
    }
    m_actionIndex = 0;

    {
        int dcA = playerCol - gridCol, drA = playerRow - gridRow;
        if (abs(dcA) >= abs(drA)) { m_aimDx = (dcA > 0) ? 1 : (dcA < 0) ? -1 : 0; m_aimDy = 0; }
        else { m_aimDx = 0; m_aimDy = (drA > 0) ? 1 : (drA < 0) ? -1 : 0; }
    }

    // ボスが2ターン攻撃を外し続けたら「移動」を足して近づく（ブロックのみのターンは維持）
    if (m_isBoss && !m_isSnake && m_missStreak >= 2 && !m_plannedActions.empty())
    {
        auto& act = m_plannedActions[0];
        bool alreadyMoves = (act.target.approach != ApproachType::None);
        for (auto& e : act.effects)
            if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway) alreadyMoves = true;

        if (!alreadyMoves)
        {
            Effect e; e.kind = EffectKind::MoveToward; e.value = 2;
            act.effects.push_back(e);
            act.description += L"＋移動";
        }
        m_missStreak = 0;   // 移動したのでリセット
    }
}

int Enemy::ExecuteAction(int actionIdx, int playerCol, int playerRow,
    GridMap* gridMap, Player* player, std::vector<Enemy*>& enemies,
    int moveTargetCol , int moveTargetRow, bool* didAttack)
{
    int mtC = (moveTargetCol >= 0) ? moveTargetCol : playerCol;
    int mtR = (moveTargetRow >= 0) ? moveTargetRow : playerRow;

    if (actionIdx < 0 || actionIdx >= (int)m_plannedActions.size()) return 0;
    m_movePath.clear();
    m_lastHitCount = 1; m_lastHitDamage = 0;
    m_didDash = false;
    const EnemyAction& act = m_plannedActions[actionIdx];
    const TargetSpec& tg = act.target;

    // 移動より前に「当たるか」を確定させる（＝予告と一致させる）
    bool aimingDecoy = (moveTargetCol >= 0 && (mtC != playerCol || mtR != playerRow));
    bool hitPlayer = tg.unavoidable
        ? !aimingDecoy                                   // 必中：デコイを狙ってなければプレイヤーへ
        : IsThreateningCell(playerCol, playerRow, act);

    bool hitTarget = IsThreateningCell(mtC, mtR, act);

    if (!tg.unavoidable)
    {
        switch (tg.approach)
        {
        case ApproachType::Toward:
            if (!hitTarget)
            {
                if (!m_plannedMovePath.empty()) MoveAlongPlanned(gridMap);          // 矢印どおりに動く
                else                            MoveToward(mtC, mtR, gridMap, tg.moveRange);  // 保険
            }
            break;
        case ApproachType::Dash:
        {
            bool dh = MoveDash(mtC, mtR, gridMap, tg.moveRange);
            hitTarget = hitTarget || dh;
            break;
        }
        case ApproachType::Align:                              
            if (!hitTarget) AlignToTarget(mtC, mtR, gridMap, tg.moveRange);
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
        case EffectKind::MoveToward: MoveToward(mtC, mtR, gridMap, e.value); break;
        case EffectKind::Coil: GrowCoil(gridMap, e.value, player); break;
        case EffectKind::MoveAway:
            MoveAway(mtC, mtR, gridMap, e.value);
            m_plannedMovePath.clear();   // 離脱後は接近矢印が残らないよう消す
            break;

        case EffectKind::Damage:
            if (hitTarget && didAttack) *didAttack = true;   // 標的が射程内＝実際に攻撃した時だけ
            if (tg.approach != ApproachType::Dash && !tg.unavoidable)
            {
                if (tg.rangeType == RangeType::Area) StartJump(1.5f, 0.4f);
                else                                 StartJump(0.5f, 0.25f);
            }
            if (hitPlayer)
            {
                int perHit = m_buffManager.GetFinalAttack(e.value);
                int h = (e.hits > 1) ? e.hits : 1;
                damage += perHit * h;
                m_lastHitDamage = perHit;   // 1ヒット分
                m_lastHitCount = h;        // ヒット数
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
                // 攻撃をブロックで防ぎ切ったら状態異常(毒など)も食らわない
                bool fullyBlocked = (damage > 0 && player && player->GetBlock() >= damage);
                if (hitPlayer && player && !fullyBlocked)
                    player->GetBuffManager().AddBuff(b);
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
        case EffectKind::PullPlayer:
            if (hitPlayer) PullPlayer(playerCol, playerRow, gridMap, player, e.value);
            break;

        case EffectKind::KnockbackPlayer:
            if (hitPlayer) KnockbackPlayer(playerCol, playerRow, gridMap, player, e.value);
            break;
        case EffectKind::Summon:
            m_pendingSummons.push_back({ e.summonId, e.value });
            break;
        case EffectKind::Hazard:
        {
            const int rows = gridMap->GetRows();
            const int cols = gridMap->GetCols();
            for (int r = 0; r < rows; r++)
                for (int c = 0; c < cols; c++)
                {
                    if (!IsInRange(c, r, tg.range, tg.rangeType, tg.minRange)) continue;
                    Cell& cell = gridMap->GetCell(c, r);
                    if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue; // 敵の足元は除外
                    if (cell.tileEffect.active) continue;                                       // 既存の床は上書きしない
                    cell.tileEffect.active = true;
                    cell.tileEffect.id = "fire";              // 既存のfire地形を流用（踏むとBurn）
                    cell.tileEffect.enemyOwned = true;        // 敵が置いたか
                    cell.tileEffect.value = e.value;          // 踏んだ時のやけし強さ
                    cell.tileEffect.duration = e.duration;    // 踏み処理用
                    cell.tileEffect.persistent = true;        // 踏んでも即消えない
                    cell.tileEffect.hazardTurns = e.duration; // Nターンで自動消滅
                }
            break;
        }
        case EffectKind::Heal:
        {
            int heal = (e.value > 0) ? e.value : damage;   // value>0=固定回復 / 0=与ダメージ分を吸収
            if (heal > 0 && hitPlayer)                       // 命中時だけ吸血
            {
                m_HP = min(m_maxHP, m_HP + heal);
                EffectManager::Play("heal", worldX, worldY + 0.5f, worldZ);
                FloatingTextManager::Spawn(worldX, worldY + 0.9f, worldZ,
                    L"+" + std::to_wstring(heal),
                    XMFLOAT4(0.4f, 1.0f, 0.5f, 1.0f), 32.0f);   // 緑で回復量
            }
            break;
        }
        }


    }

    // 命中したらプリセットのエフェクトを命中点で再生
    if (hitPlayer && !act.vfx.empty() && player)
        EffectManager::Play(act.vfx, player->worldX, player->worldY + 0.5f, player->worldZ);

    // この行動がダメージ攻撃なら、命中したか(=避けられなかったか)を記録
    bool isAttack = false;
    for (auto& e : act.effects)
        if (e.kind == EffectKind::Damage) { isAttack = true; break; }

    if (isAttack)
    {
        m_lastAttackWhiffed = !hitPlayer;
        if (hitPlayer) m_missStreak = 0;   // 当たったらリセット
        else           m_missStreak++;     // 外したら加算（ブロックのみは攻撃でないので不変）
    }

    // 移動しようとしたのに0マスだった行動でも「行動した」のが分かるよう小さくにじり寄る
    bool movedThisAction = !m_movePath.empty() || m_didDash;
    bool wantsToward = (tg.approach == ApproachType::Toward || tg.approach == ApproachType::Dash);
    bool wantsAway = false;
    for (auto& e : act.effects)
    {
        if (e.kind == EffectKind::MoveToward) wantsToward = true;
        if (e.kind == EffectKind::MoveAway)   wantsAway = true;
    }
    if ((wantsToward || wantsAway) && !movedThisAction && !isAttack
        && !IsJumping() && !IsLunging() && !IsMoving())
    {
        float px = (playerCol - gridMap->GetCols() / 2.0f) * 1.1f;
        float pz = (playerRow - gridMap->GetRows() / 2.0f) * 1.1f;
        if (wantsAway) { px = worldX - (px - worldX); pz = worldZ - (pz - worldZ); }  // 逃げ行動は反対側へ
        StartLunge(px, pz, 0.28f);
    }

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

void Enemy::ApplyDifficulty(float hpMul, float dmgMul, int bonusActions)
{
    m_maxHP = (int)(m_maxHP * hpMul);
    m_HP = m_maxHP;
    m_displayHp = (float)m_HP;
    m_dmgScale = dmgMul;
    m_bonusActions = bonusActions;
}

// 目標へ1マス貪欲移動（隣接で停止）。動けなければfalse
static bool StepToward(int& c, int& r, int tc, int tr, GridMap* g, std::vector<std::pair<int, int>>& path)
{
    int dc = tc - c, dr = tr - r;
    if (abs(dc) + abs(dr) <= 1) return false;
    std::pair<int, int> cand[2]; int n = 0;
    if (abs(dc) >= abs(dr)) { cand[n++] = { c + (dc > 0 ? 1 : -1), r }; if (dr != 0) cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; }
    else { cand[n++] = { c, r + (dr > 0 ? 1 : -1) }; if (dc != 0) cand[n++] = { c + (dc > 0 ? 1 : -1), r }; }
    for (int k = 0; k < n; k++)
    {
        int mx = cand[k].first, my = cand[k].second;
        if (mx < 0 || mx >= g->GetCols() || my < 0 || my >= g->GetRows()) continue;
        if (g->GetCell(mx, my).type != CellType::Empty) continue;
        c = mx; r = my; path.push_back({ c, r }); return true;
    }
    return false;
}

std::vector<std::pair<int, int>> Enemy::PlannedMovePath(int targetCol, int targetRow, GridMap* gridMap) const
{
    std::vector<std::pair<int, int>> path;
    const EnemyAction* a = GetNextAction();
    if (!a) return path;
    const TargetSpec& tg = a->target;

    // 攻撃の接近（Dash / Toward）
    if (!tg.unavoidable && tg.moveRange > 0)
    {
        if (tg.approach == ApproachType::Dash)
        {
            if (m_aimDx == 0 && m_aimDy == 0) return path;
            int c = gridCol, r = gridRow;
            for (int i = 0; i < tg.moveRange; i++)
            {
                int nc = c + m_aimDx, nr = r + m_aimDy;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
                if (nc == targetCol && nr == targetRow) break;
                if (gridMap->GetCell(nc, nr).type != CellType::Empty) break;
                c = nc; r = nr; path.push_back({ c, r });
            }
            return path;
        }
        if (tg.approach == ApproachType::Toward)
        {
            if (IsThreateningCell(targetCol, targetRow, *a)) return path;   // 既に当たるなら接近しない（実行側と一致）
            int c = gridCol, r = gridRow;
            for (int s = 0; s < tg.moveRange; s++) if (!StepToward(c, r, targetCol, targetRow, gridMap, path)) break;
            return path;
        }
        if (tg.approach == ApproachType::Align)
        {
            int c = gridCol, r = gridRow;
            for (int s = 0; s < tg.moveRange; s++)
            {
                int dc = targetCol - c, dr = targetRow - r;
                if (dc == 0 || dr == 0) break;                    // 十字線上＝整列済み
                int mc = 0, mr = 0;
                if (abs(dc) <= abs(dr)) mc = (dc > 0) ? 1 : -1;   // 小さい方の軸を詰める（距離は保つ）
                else                    mr = (dr > 0) ? 1 : -1;
                int nc = c + mc, nr = r + mr;
                if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) break;
                if (gridMap->GetCell(nc, nr).type != CellType::Empty) break;
                c = nc; r = nr; path.push_back({ c, r });
            }
            return path;
        }
    }

    // 純移動効果（MoveToward / MoveAway）
    for (auto& e : a->effects)
    {
        if (e.kind == EffectKind::MoveToward)
        {
            int c = gridCol, r = gridRow;
            for (int s = 0; s < e.value; s++) if (!StepToward(c, r, targetCol, targetRow, gridMap, path)) break;
            return path;
        }
        if (e.kind == EffectKind::MoveAway)
        {
            const int dirs[4][2] = { {0,1},{0,-1},{1,0},{-1,0} };
            int c = gridCol, r = gridRow;
            for (int s = 0; s < e.value; s++)
            {
                int curD = abs(targetCol - c) + abs(targetRow - r);
                int bc = c, br = r, bd = curD;
                for (auto& d : dirs)
                {
                    int nc = c + d[0], nr = r + d[1];
                    if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
                    if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
                    int nd = abs(targetCol - nc) + abs(targetRow - nr);
                    if (nd > bd) { bd = nd; bc = nc; br = nr; }
                }
                if (bd == curD) break;
                c = bc; r = br; path.push_back({ c, r });
            }
            return path;
        }
    }
    return path;
}

void Enemy::InitSnake(GridMap* gridMap)
{
    if (!m_isSnake) return;
    int cols = gridMap->GetCols(), rows = gridMap->GetRows();
    m_spiral.clear(); m_bodyCells.clear(); m_spiralIdx = 0;
    int top = 0, bot = rows - 1, left = 0, right = cols - 1;   // 外周→内へ渦巻き
    while (top <= bot && left <= right)
    {
        for (int c = left; c <= right; c++) m_spiral.push_back({ c, top });   top++;
        for (int r = top; r <= bot; r++) m_spiral.push_back({ right, r });  right--;
        if (top <= bot) { for (int c = right; c >= left; c--) m_spiral.push_back({ c, bot }); bot--; }
        if (left <= right) { for (int r = bot;   r >= top;  r--) m_spiral.push_back({ left, r }); left++; }
    }
    gridMap->SetCellType(gridCol, gridRow, CellType::Empty);   // 元の位置を空ける
    gridCol = m_spiral[0].first; gridRow = m_spiral[0].second; // 頭を渦巻き先頭へ
    gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
    worldX = (gridCol - cols / 2.0f) * 1.1f;
    worldZ = (gridRow - rows / 2.0f) * 1.1f;
}

void Enemy::GrowCoil(GridMap* gridMap, int steps, Player* player)
{
    if (!m_isSnake || m_spiral.empty()) return;

    // 中央到達後は伸ばさず、数ターン待ってからフィニッシャー解禁
    if (m_coilReached)
    {
        if (++m_coilStuck >= 4) m_coilComplete = true;   // この 2 を増やすほど遅い
    }

    m_lastHitHead = false;                       // とぐろ移動の罠は2倍対象外
    std::vector<std::pair<float, float>> pts;
    for (int s = 0; s < steps; s++)
    {
        if (m_spiralIdx + 1 >= (int)m_spiral.size()) break;

        auto nx = m_spiral[m_spiralIdx + 1];
        CellType t = gridMap->GetCell(nx.first, nx.second).type;
        if (t == CellType::Wall) break;
        if (t == CellType::Player)
        {
            if (player) player->TakeDamage(8);              // 轢かれダメージ（攻撃とは別）
            if (!PushPlayerToCenter(gridMap, player)) break;
        }
        // 以降、頭を nx へ進める（既存のまま）
        m_bodyCells.push_back({ gridCol, gridRow });
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        gridCol = nx.first; gridRow = nx.second;
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        m_spiralIdx++;
        m_movePath.push_back({ gridCol, gridRow });        // 通過マス（罠判定用）
        int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
        if (abs(gridCol - cc) + abs(gridRow - cr) <= 1) m_coilReached = true;
        pts.push_back({ (gridCol - gridMap->GetCols() / 2.0f) * 1.1f,
                        (gridRow - gridMap->GetRows() / 2.0f) * 1.1f });
    }
    if (!pts.empty()) StartWalk(pts, 0.1f);
}

bool Enemy::PushPlayerToCenter(GridMap* gridMap, Player* player)
{
    if (!player) return false;
    int pc = player->gridCol, pr = player->gridRow;
    int cc = gridMap->GetCols() / 2, cr = gridMap->GetRows() / 2;
    int dc = cc - pc, dr = cr - pr;

    std::vector<std::pair<int, int>> tries;
    if (abs(dc) >= abs(dr)) { if (dc) tries.push_back({ dc > 0 ? 1 : -1, 0 }); if (dr) tries.push_back({ 0, dr > 0 ? 1 : -1 }); }
    else { if (dr) tries.push_back({ 0, dr > 0 ? 1 : -1 }); if (dc) tries.push_back({ dc > 0 ? 1 : -1, 0 }); }
    // フォールバック：中央方向が塞がってたら空いてる方へ押し出す
    tries.push_back({ 1,0 }); tries.push_back({ -1,0 }); tries.push_back({ 0,1 }); tries.push_back({ 0,-1 });

    for (auto& [sx, sy] : tries)
    {
        int nc = pc + sx, nr = pr + sy;
        if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
        if (gridMap->GetCell(nc, nr).type != CellType::Empty) continue;
        gridMap->SetCellType(pc, pr, CellType::Empty);
        player->gridCol = nc; player->gridRow = nr;
        gridMap->SetCellType(nc, nr, CellType::Player);
        std::vector<std::pair<float, float>> pts = {
            { (nc - gridMap->GetCols() / 2.0f) * 1.1f, (nr - gridMap->GetRows() / 2.0f) * 1.1f } };
        player->StartWalk(pts, 0.08f);
        return true;
    }
    return false;   // 完全に囲まれてて押せない
}

bool Enemy::OccupiesCell(int col, int row) const
{
    for (auto& [dc, dr] : m_gridShape)
        if (gridCol + dc == col && gridRow + dr == row) return true;
    if (m_isSnake)
        for (auto& b : m_bodyCells)
            if (b.first == col && b.second == row) return true;
    return false;
}