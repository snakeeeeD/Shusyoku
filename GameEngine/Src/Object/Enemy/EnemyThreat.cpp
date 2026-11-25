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

// 攻撃範囲と、当たるかの判定
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

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

// 噛みつき（首を伸ばす）の行動か
bool Enemy::HasLunge(const EnemyAction& a)
{
    for (auto& e : a.effects) if (e.kind == EffectKind::Lunge) return true;
    return false;
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
