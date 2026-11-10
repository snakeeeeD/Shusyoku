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
#include <algorithm>
#include <map>
#include <queue>

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
    m_drawZ = data->drawZ;
    m_isSnake = data->snake;
}

void Enemy::Update(float delteTime)
{

}

void Enemy::Draw3D(Renderer3D* renderer)
{
    if (!isActive) return;
    if (m_isSnake) return;

    XMFLOAT4 drawColor = GetDrawColor();

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
        worldX, y, worldZ + m_drawZ,
        width * s, height * s, 0.0f, drawColor, FacingUV());

    // 重ねる絵は本体とまったく同じ位置・大きさ（絵の中で配置済み）
    for (auto& o : m_overlays)
        renderer->DrawBillboard(TextureManager::Get(o),
            worldX, y, worldZ + m_drawZ,
            width * s, height * s, 0.0f, drawColor, FacingUV());
}

bool Enemy::RemoveOverlay(const std::string& t)
{
    auto it = std::find(m_overlays.begin(), m_overlays.end(), t);
    if (it == m_overlays.end()) return false;
    m_overlays.erase(it);
    return true;
}

// プレイヤーの方を向く。画像は左向きが基準なので、右にいる時だけ反転する
void Enemy::FaceToward(float targetX)
{
    if (m_isBoss) return;                           // ボスは向き固定
    float dx = targetX - worldX;
    if (dx > 0.05f) m_faceRight = true;            // 真上・真下にいる時は前の向きを保つ（ちらつき防止）
    if (dx < -0.05f) m_faceRight = false;
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

// 形の中心のワールド座標（2*2 ならマスの間に来る）
void Enemy::CenterWorld(int col, int row, GridMap* gridMap, float& x, float& z) const
{
    float sc = 0, sr = 0;
    for (auto& [dc, dr] : m_gridShape) { sc += dc; sr += dr; }
    float n = (float)m_gridShape.size();
    x = (col + sc / n - gridMap->GetCols() / 2.0f) * 1.1f;
    z = (row + sr / n - gridMap->GetRows() / 2.0f) * 1.1f;
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
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
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
    cell.tileEffect.hazardTurns = m_buffManager.GetBuffValue(BuffType::EmberTrail);   // 付与するバフの数字がターン数
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

        // 4方向を「同じ現在地」から評価して、一番離れられる隣へ
        int bestCol = gridCol, bestRow = gridRow, bestDist = curDist;
        for (int d = 0; d < 4; d++)
        {
            int nc = gridCol + dirs[d][0];
            int nr = gridRow + dirs[d][1];
            if (nc < 0 || nc >= gridMap->GetCols() || nr < 0 || nr >= gridMap->GetRows()) continue;
            if (!CanOccupy(gridMap, nc, nr)) continue;
            int nd = abs(playerCol - nc) + abs(playerRow - nr);
            if (nd > bestDist) { bestDist = nd; bestCol = nc; bestRow = nr; }
        }

        if (bestDist == curDist) break;   // どこへ動いても離れられない＝行き止まり

        // ここで初めて実際に移動（footprintで多マスも安全に）
        ClearFootprint(gridMap);
        gridCol = bestCol; gridRow = bestRow;
        MarkFootprint(gridMap);
        m_movePath.push_back({ gridCol, gridRow });

        { float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz); pts.push_back({ wx, wz }); }   // 通過マス
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
        for (auto& [dc, dr] : GetGridShape())               // 体のどこかがぶつかった＝ヒット
            if (nc + dc == playerCol && nr + dr == playerRow) hit = true;
        if (hit) break;
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
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
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
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
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
    {
        float wx, wz; CenterWorld(gridCol, gridRow, gridMap, wx, wz);
        StartMove(wx, wz);
    }
}

void Enemy::TakeDamage(int damage, DamageFeel feel)
{
    if (IsInvulnerable())
    {
        // 無敵：ダメージ完全無効（表記0＋ガード演出）
        EffectManager::Play("defend", worldX, worldY + height * 0.5f, worldZ);
        DamageFeedback::Play(DamageFeel::Hit, worldX, worldY + height * 0.5f, worldZ, 0, 0);
        return;
    }

    if (feel == DamageFeel::Poison || feel == DamageFeel::Holy)
    {
        if (m_isSnake && feel == DamageFeel::Poison) damage = max(1, damage / 2);   // 毒蛇に毒は効きにくい
        // 毒・聖地の焼きはブロック/弱体を無視して表示値通り与える
        m_HP -= damage;
        if (m_HP < 0) m_HP = 0;
        DamageFeedback::Play(feel, worldX, worldY + height * 0.5f, worldZ, damage, 0);
        return;
    }

    Audio::PlaySE("Assets/Sound/se/hit.mp3");

    if (m_isSnake && m_hitPart == HitPart::Head) damage *= 2;                   // 頭は弱点（2倍）
    if (m_isSnake && m_hitPart == HitPart::Body) damage = max(1, damage / 2);   // 体は硬い（カードの攻撃だけ半分）
    if (m_hitWeak && m_hitPart == HitPart::Head) m_weakCell = { -1, -1 };   // 光るウロコは1回で割れる
    m_hitWeak = false;
    if (m_isSnake && m_hitPart != HitPart::None)          // カードで殴られた分だけ数える（毒・罠は数えない）
    {
        m_cardDamage += damage;
        if (m_cardDamage >= SHED_DAMAGE)
        {
            m_cardDamage -= SHED_DAMAGE;
            m_coilBonus += 2;                             // 巻きつきが速くなる
            m_buffManager.AddBuff({ BuffType::AttackUp, 2, -1 });   // 攻撃力も上がる（速いと殴りやすくなる分のリスク）
            m_shedPending = true;
        }
        m_buffManager.AddBuff({ BuffType::Molt, SHED_DAMAGE - m_cardDamage, -1 });   // 残りの表示を更新
    }
    m_hitPart = HitPart::None;

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

void Enemy::SetInvulnerable(bool v)
{
    bool has = m_buffManager.HasBuff(BuffType::Invincible);
    if (v && !has)       m_buffManager.AddBuff({ BuffType::Invincible, 0, -1 });   // 永続
    else if (!v && has)  m_buffManager.RemoveBuff(BuffType::Invincible);
}

void Enemy::AddBlock(int amount)
{
    if (amount > 0)
        EffectManager::Play("defend", worldX, worldY + height * 0.5f, worldZ);
    m_block += amount;
}

void Enemy::ResetBlock()
{
    if (!m_buffManager.HasBuff(BuffType::Barricade)) m_block = 0;   // バリケード中は持ち越す（プレイヤーと同じ）
}

// 噛みつき（首を伸ばす）の行動か
static bool HasLunge(const EnemyAction& a)
{
    for (auto& e : a.effects) if (e.kind == EffectKind::Lunge) return true;
    return false;
}

// 行動に移動が含まれるか（接近・離脱・引き寄せ）。含むなら棒立ち防止の「＋移動」を足さない
static bool ActionAlreadyMoves(const EnemyAction& act)
{
    if (act.target.approach != ApproachType::None) return true;
    for (auto& e : act.effects)
        if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway
            || e.kind == EffectKind::PullPlayer) return true;
    return false;
}

void Enemy::PullPlayer(int playerCol, int playerRow, GridMap* gridMap, Player* player, int steps)
{
    if (!player) return;
    if (player->GetBuffManager().HasBuff(BuffType::MoveLock)) return;   // 踏ん張り中は引き寄せられない
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
    if (player->GetBuffManager().HasBuff(BuffType::MoveLock)) return;   // 踏ん張り中は吹き飛ばない
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
    if (a.select.condition == "lungeReady") return m_neckLen == 0 && !m_coilComplete && m_turnsSinceLunge >= 3;
    if (a.select.condition == "lunged")     return m_neckLen > 0;
    if (a.select.condition == "invuln") return IsInvulnerable();   // 無敵中のみ
    if (a.select.condition == "awake")  return !IsInvulnerable();  // 覚醒後のみ
    return true;
}

void Enemy::DecideNextAction(int playerCol, int playerRow, int turn)
{
    const EnemyData* data = EnemyDataBase::Get(m_id);
    if (!data || data->actions.empty())
    {
       return;
    }

    m_lockC = playerCol; m_lockR = playerRow;   // Playerアンカーの中心を記録
    if (m_isSnake) { PickWeakCell(playerCol, playerRow); m_turnsSinceLunge++; }

    // 前回の決定時から動いたか（棒立ち検知）
    if (gridCol == m_lastDecideCol && gridRow == m_lastDecideRow) m_idleTurns++;
    else m_idleTurns = 0;
    m_lastDecideCol = gridCol; m_lastDecideRow = gridRow;

    if (m_actionUses.size() != data->actions.size()) m_actionUses.assign(data->actions.size(), 0);
    auto usedUp = [&](const EnemyAction& a) {   // 1戦で使える回数を使い切ったか（maxUses=0は無制限）
        return a.select.maxUses > 0 && m_actionUses[&a - &data->actions[0]] >= a.select.maxUses; };

    // 順番モード：行動を定義順に1つずつ回す
    if (data->sequential)
    {
        // 条件付き行動が満たされていれば最優先
        const EnemyAction* picked = nullptr;
        for (auto& a : data->actions)
            if (!a.select.condition.empty() && !usedUp(a) && ConditionMet(a, playerCol, playerRow, turn))
            {
                picked = &a; break;
            }

        if (!picked)
        {
            // 無条件行動だけを定義順に回す
            std::vector<const EnemyAction*> seq;
            for (auto& a : data->actions)
                if (a.select.condition.empty() && !usedUp(a)) seq.push_back(&a);
            if (!seq.empty()) { picked = seq[m_seqIndex % (int)seq.size()]; m_seqIndex++; }
            else picked = &data->actions[0];
        }

        m_plannedActions.clear();
        m_plannedActions.push_back(*picked);
        m_actionUses[picked - &data->actions[0]]++;
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
            if (!ActionAlreadyMoves(act))
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
        if (usedUp(a)) continue;   // 使い切った行動は選ばない
        if (!ConditionMet(a, playerCol, playerRow, turn)) continue;
        if (a.select.condition.empty()) { uncond.push_back(&a); totalU += a.select.chance; }
        else { cond.push_back(&a);   totalC += a.select.chance; }
    }

    // 首を戻す・噛みつきは、他の条件より優先
    for (const char* must : { "lunged", "lungeReady" })
        for (auto* a : cond)
            if (a->select.condition == must) { cond = { a }; totalC = max(1, a->select.chance); break; }

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
        m_actionUses[picked - &data->actions[0]]++;
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
        if (!ActionAlreadyMoves(act))
        {
            Effect e; e.kind = EffectKind::MoveToward; e.value = 2;
            act.effects.push_back(e);
            act.description += L"＋移動";
        }
        m_missStreak = 0;   // 移動したのでリセット
    }

    m_summonPlan.clear();
    for (auto& act : m_plannedActions) PlanSummon(act);

    m_lungePlan.clear();
    if (!m_plannedActions.empty() && HasLunge(m_plannedActions[0]))
        m_lungePlan = LungePath(playerCol, playerRow);
}

void Enemy::SetAwakenIntent()
{
    EnemyAction a;
    a.description = L"覚醒";
    a.target.rangeType = RangeType::None;   // 範囲なし＝脅威マーク・ダメージ予告が出ない
    m_plannedActions.clear();
    m_plannedActions.push_back(a);
    m_actionIndex = 0;
}

int Enemy::ExecuteAction(int actionIdx, int playerCol, int playerRow,
    GridMap* gridMap, Player* player, std::vector<Enemy*>& enemies,
    int moveTargetCol , int moveTargetRow, bool* didAttack)
{
    int mtC = (moveTargetCol >= 0) ? moveTargetCol : playerCol;
    int mtR = (moveTargetRow >= 0) ? moveTargetRow : playerRow;

    if (actionIdx < 0 || actionIdx >= (int)m_plannedActions.size()) return 0;
    m_movePath.clear();
    m_pendingPlayerBuffs.clear();
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
        case EffectKind::Coil: GrowCoil(gridMap, e.value + m_coilBonus, player); break;
        case EffectKind::Lunge:   LungeTo(gridMap); break;
        case EffectKind::Retract: RetractNeck(gridMap); break;
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
            if (e.applyTo == ApplyTo::Allies)
            {
                int blk = m_buffManager.GetFinalBlock(e.value);
                for (auto ally : enemies)
                    if (ally && ally->GetHp() > 0 && !ally->IsDying())
                        ally->AddBlock(blk);   // 自分含む全味方に付与
            }
            else
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
                // ダメージ適用後に付与する（弱体が自分の攻撃を強化しないように）
                if (hitPlayer && player) m_pendingPlayerBuffs.push_back(b);
            }
            else // Allies：全ての味方に付与（範囲無視・自分は除く）
            {
                for (auto other : enemies)
                {
                    if (other == this || !other) continue;
                    if (other->GetHp() <= 0 || other->IsDying()) continue;
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
                    bool inArea = (tg.anchor != AnchorType::Self)
                        ? IsCellInTarget(c, r, tg)                               // プレイヤー/座標が中心の攻撃は、狙った場所に置く
                        : IsInRange(c, r, tg.range, tg.rangeType, tg.minRange);  // 敵が中心（従来どおり）
                    if (!inArea) continue;
                    Cell& cell = gridMap->GetCell(c, r);
                    if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue; // 敵の足元は除外
                    if (cell.tileEffect.active) continue;                                       // 既存の床は上書きしない
                    cell.tileEffect.active = true;
                    cell.tileEffect.id = e.buff.empty() ? "fire" : e.buff;   // 床の種類（terrains.json。省略時は炎）
                    cell.tileEffect.enemyOwned = true;        // 敵が置いたか
                    cell.tileEffect.value = e.value;          // 踏んだ時の強さ
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
        case EffectKind::AddCard:
            if (hitPlayer || tg.rangeType == RangeType::None)   // 範囲系は命中時だけ／None(必中的)は常時
                m_pendingCurses.push_back({ e.summonId, e.buff.empty() ? "deck" : e.buff, e.value > 0 ? e.value : 1 });
            break;
        }


    }

    // 命中したらエフェクトを命中点で再生（未指定ならスマッシュ）
    if (hitPlayer && player)
    {
        std::string fx = act.vfx.empty() ? "smash" : act.vfx;
        EffectManager::Play(fx, player->worldX, player->worldY + 0.5f, player->worldZ);
    }

    // 自分に向けた演出（バフ・構えなど）は、当たったかに関係なく自分の位置で出す
    // 絵と同じ位置に合わせる（絵は drawZ だけ手前に描いている／高さは体の真ん中）
    if (!act.selfVfx.empty())
        EffectManager::Play(act.selfVfx, worldX, worldY + height * 0.5f, worldZ + m_drawZ);

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

bool Enemy::IsCellInTarget(int col, int row, const TargetSpec& tg) const
{
    if (tg.unavoidable) return true;
    if (tg.approach == ApproachType::Dash)
    {
        for (int i = 1; i <= tg.moveRange; i++)
            for (auto& [dc, dr] : GetGridShape())           // 体の幅ぶん
                if (gridCol + dc + m_aimDx * i == col && gridRow + dr + m_aimDy * i == row) return true;
        return false;
    }
    if (tg.anchor != AnchorType::Self)
    {
        int ac = (tg.anchor == AnchorType::Player) ? m_lockC : tg.anchorCol;
        int ar = (tg.anchor == AnchorType::Player) ? m_lockR : tg.anchorRow;
        int rng = tg.range + m_buffManager.GetBuffValue(BuffType::RangeUp);
        return RangeShape::Contains(ac, ar, col, row, tg.rangeType, rng, tg.minRange, m_aimDx, m_aimDy);
    }
    return IsInRange(col, row, tg.range, tg.rangeType, tg.minRange);
}

// 生成位置を決める（行動を決めた時に呼ぶ）。target の範囲の空きマスからランダムに選ぶ
void Enemy::PlanSummon(const EnemyAction& a)
{
    if (!m_grid) return;
    for (auto& e : a.effects)
    {
        if (e.kind != EffectKind::Summon || a.target.rangeType == RangeType::None) continue;
        std::vector<std::pair<int, int>> cells;
        for (int r = 0; r < m_gridRows; r++)
            for (int c = 0; c < m_gridCols; c++)
                if (m_grid->GetCell(c, r).type == CellType::Empty && IsCellInTarget(c, r, a.target))
                    cells.push_back({ c, r });
        for (int i = (int)cells.size() - 1; i > 0; i--) std::swap(cells[i], cells[rand() % (i + 1)]);
        if ((int)cells.size() > e.value) cells.resize(e.value);
        m_summonPlan.insert(m_summonPlan.end(), cells.begin(), cells.end());
    }
}

bool Enemy::IsThreateningCell(int col, int row, const EnemyAction& a) const
{
    for (auto& c : m_summonPlan)                        // 生成位置も範囲として見せる
        if (c.first == col && c.second == row) return true;
    if (IsCellInTarget(col, row, a.target)) return true;
    for (auto& t : a.extraTargets)
        if (IsCellInTarget(col, row, t)) return true;
    if (HasLunge(a))                                    // 首の通り道に立つと噛まれる
        for (auto& c : m_lungePlan)
            if (c.first == col && c.second == row) return true;
    return false;
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
    if (HasLunge(*a)) return m_lungePlan;   // 噛みつきは予告の経路そのもの
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
                if (!CanOccupy(gridMap, nc, nr)) break;             // 形全体で判定（プレイヤーのマスも含む）
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
        if (e.kind == EffectKind::Coil)
        {
            for (int k = 0; k < m_neckLen; k++)             // 首を伸ばしている時は、まず首を戻す道を通る
                path.push_back(m_bodyCells[m_bodyCells.size() - 1 - k]);
            if (m_coilReached || m_buffManager.HasBuff(BuffType::Root)) return path;   // 中央到達後・拘束中は巻かない
            for (int s = 1; s <= e.value + m_coilBonus; s++)   // 脱皮で速くなった分も含める
            {
                int idx = m_spiralIdx + s;
                if (idx >= (int)m_spiral.size()) break;
                path.push_back(m_spiral[idx]);
            }
            return path;
        }
    }
    return path;
}

// 光るウロコ：プレイヤーから4マス以内で、渦の一番内側の体からランダムに選ぶ
void Enemy::PickWeakCell(int playerCol, int playerRow)
{
    constexpr int MAX_DIST = 4;
    m_weakCell = { -1, -1 };
    int bestRing = -1, found = 0, nearest = 1 << 30;
    std::pair<int, int> fallback{ -1, -1 };
    for (int i = 0; i < (int)m_bodyCells.size(); i++)
    {
        auto& b = m_bodyCells[i];
        int d = abs(b.first - playerCol) + abs(b.second - playerRow);
        if (d < nearest) { nearest = d; fallback = b; }
        if (d > MAX_DIST) continue;
        int ring = min(min(b.first, m_gridCols - 1 - b.first), min(b.second, m_gridRows - 1 - b.second));   // 外周=0、内側ほど大きい
        if (ring > bestRing) { bestRing = ring; found = 0; }
        if (ring == bestRing && rand() % ++found == 0) m_weakCell = b;
    }
    if (m_weakCell.first < 0) m_weakCell = fallback;   // 4マス以内に体がなければ一番近い体
}

void Enemy::InitSnake(GridMap* gridMap)
{
    if (!m_isSnake) return;
    int cols = gridMap->GetCols(), rows = gridMap->GetRows();
    m_gridCols = cols; m_gridRows = rows;
    m_grid = gridMap;
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

// 蛇の1マスの時間：長い距離ほど速く（base より遅くはしない）
static float SnakeStepDur(int cells, float base)
{
    return min(base, 0.03f + 0.2f / cells);
}

void Enemy::GrowCoil(GridMap* gridMap, int steps, Player* player)
{
    if (!m_isSnake || m_spiral.empty()) return;
    if (m_buffManager.HasBuff(BuffType::Root)) return;   // 拘束中は巻きつきも、まるのみの準備も止まる

    // 中央到達後は伸ばさず、数ターン待ってからフィニッシャー解禁
    if (m_coilReached)
    {
        if (++m_coilStuck >= 4) m_coilComplete = true;   // この 2 を増やすほど遅い
    }

    m_hitPart = HitPart::None;                       // とぐろ移動の罠は2倍対象外
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
        QueueSnakeOp(true, nx);
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
    if (!pts.empty())                          // 首を戻す歩きの続きにつなぐ（合わせた距離で速さを決める）
        AppendWalk(pts, SnakeStepDur((int)(m_walkPoints.size() + pts.size()), 0.1f));
}

// 頭からプレイヤーのマスまでの経路（空きマスだけを通る、最大8マス）
std::vector<std::pair<int, int>> Enemy::LungePath(int aimCol, int aimRow) const
{
    constexpr int MAX_LEN = 8;
    static const int DIRS[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    std::map<std::pair<int, int>, std::pair<int, int>> from;
    std::queue<std::pair<std::pair<int, int>, int>> q;
    std::pair<int, int> start{ gridCol, gridRow }, goal{ aimCol, aimRow };
    q.push({ start, 0 }); from[start] = start;
    bool found = false;
    while (!q.empty() && !found)
    {
        auto [p, d] = q.front(); q.pop();
        if (p == goal) { found = true; break; }
        if (d >= MAX_LEN) continue;
        for (auto& dir : DIRS)
        {
            std::pair<int, int> n{ p.first + dir[0], p.second + dir[1] };
            if (n.first < 0 || n.first >= m_gridCols || n.second < 0 || n.second >= m_gridRows) continue;
            bool free = (n == goal) || m_grid->GetCell(n.first, n.second).type == CellType::Empty;
            if (from.count(n) || !free) continue;
            from[n] = p; q.push({ n, d + 1 });
        }
    }
    std::vector<std::pair<int, int>> path;
    if (!found) return path;
    for (auto p = goal; p != start; p = from[p]) path.push_back(p);
    std::reverse(path.begin(), path.end());
    return path;
}

// 噛みつき：予告した経路を進む。空いていないマス（プレイヤーなど）の手前で止まる
void Enemy::LungeTo(GridMap* gridMap)
{
    m_turnsSinceLunge = 0;
    std::vector<std::pair<float, float>> pts;
    for (auto& c : m_lungePlan)
    {
        if (gridMap->GetCell(c.first, c.second).type != CellType::Empty) break;
        QueueSnakeOp(true, c);
        m_bodyCells.push_back({ gridCol, gridRow });
        gridCol = c.first; gridRow = c.second;
        gridMap->SetCellType(gridCol, gridRow, CellType::Boss);
        m_movePath.push_back(c);
        pts.push_back({ (gridCol - m_gridCols / 2.0f) * 1.1f, (gridRow - m_gridRows / 2.0f) * 1.1f });
        m_neckLen++;
    }
    m_lungePlan.clear();
    m_plannedMovePath.clear();   // 手前で止まっても矢印が残らないよう消す
    if (!pts.empty()) StartWalk(pts, SnakeStepDur((int)pts.size(), 0.06f));
}

// 体の見た目の操作をためる（最初の1つの時に、今の体を土台として保存）
void Enemy::QueueSnakeOp(bool push, std::pair<int, int> cell)
{
    if (m_snakeOps.empty())
    {
        m_visualBase = m_bodyCells;
        m_visualBase.push_back({ gridCol, gridRow });
        m_opApplied = 0;
    }
    m_snakeOps.push_back({ push, cell });
}

// 描画用の体の列：頭の歩きの進み具合に合わせて、操作を1つずつ反映する
std::vector<std::pair<int, int>> Enemy::VisualChain()
{
    bool walking = IsMoving() || !m_walkPoints.empty();
    if (!walking) { m_snakeOps.clear(); m_visualBase.clear(); }
    if (m_snakeOps.empty())
    {
        auto chain = m_bodyCells;
        chain.push_back({ gridCol, gridRow });
        return chain;
    }
    int done = m_walkIndex;                                       // 歩き終えたマス数
    while (m_opApplied < (int)m_snakeOps.size())
    {
        auto& op = m_snakeOps[m_opApplied];
        if (op.push ? m_opApplied < done : m_opApplied <= done)   // 伸ばす＝通過後、戻す＝歩き始めに反映
        {
            if (op.push) m_visualBase.push_back(op.cell); else m_visualBase.pop_back();
            m_opApplied++;
        }
        else break;
    }
    auto chain = m_visualBase;
    if (done < (int)m_snakeOps.size() && m_snakeOps[done].push)
        chain.push_back(m_snakeOps[done].cell);                   // 今向かっているマス（頭の向きと曲がり角のため）
    return chain;
}

void Enemy::RetractNeck(GridMap* gridMap)
{
    std::vector<std::pair<float, float>> pts;
    for (; m_neckLen > 0; m_neckLen--)
    {
        QueueSnakeOp(false, m_bodyCells.back());
        gridMap->SetCellType(gridCol, gridRow, CellType::Empty);
        auto b = m_bodyCells.back(); m_bodyCells.pop_back();
        gridCol = b.first; gridRow = b.second;
        pts.push_back({ (gridCol - m_gridCols / 2.0f) * 1.1f, (gridRow - m_gridRows / 2.0f) * 1.1f });
    }
    if (!pts.empty()) StartWalk(pts, SnakeStepDur((int)pts.size(), 0.06f));
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