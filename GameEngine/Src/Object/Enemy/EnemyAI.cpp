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
#include "EffectDataBase.h"   // 溶ける見た目の材質（fx_dissolve）
#include "CardExecutor.h"     // 床の効果（TriggerTerrain）
#include "RelicManager.h"     // 床の効果を受けないレリック
#include <algorithm>
#include <map>
#include <queue>

// 次の行動を決める（条件・順番・覚醒の予告・弱点のマス・召喚の予定）
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

// 行動に移動が含まれるか（接近・離脱・引き寄せ）。含むなら棒立ち防止の「＋移動」を足さない
static bool ActionAlreadyMoves(const EnemyAction& act)
{
    if (act.target.approach != ApproachType::None) return true;
    for (auto& e : act.effects)
        if (e.kind == EffectKind::MoveToward || e.kind == EffectKind::MoveAway
            || e.kind == EffectKind::PullPlayer) return true;
    return false;
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
