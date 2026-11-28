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

// 効果：召喚・床を置く

// 召喚（実際に呼ぶのは BattleScene）
void Enemy::EffectSummon(ActionRun& r, const Effect& e)
{
    m_pendingSummons.push_back({ e.summonId, e.value });
}

// 卵がかえる（その場で summonId の敵と入れ替えるのは BattleScene。倒した扱いにしない）
void Enemy::EffectHatch(ActionRun& r, const Effect& e)
{
    m_pendingHatch = e.summonId;
}

// 床を置く（狙った場所／敵の周り。敵の足元と、すでにある床は避ける）
void Enemy::EffectHazard(ActionRun& r, const Effect& e)
{
    const int rows = r.gridMap->GetRows();
    const int cols = r.gridMap->GetCols();
    for (int row = 0; row < rows; row++)   // 行（r は ActionRun なので row）
        for (int c = 0; c < cols; c++)
        {
            bool inArea = (r.tg.anchor != AnchorType::Self)
                ? IsCellInTarget(c, row, r.tg)                               // プレイヤー/座標が中心の攻撃は、狙った場所に置く
                : IsInRange(c, row, r.tg.range, r.tg.rangeType, r.tg.minRange);  // 敵が中心（従来どおり）
            if (!inArea) continue;
            Cell& cell = r.gridMap->GetCell(c, row);
            if (cell.type == CellType::Enemy || cell.type == CellType::Boss) continue; // 敵の足元は除外
            if (cell.tileEffect.active) continue;                                       // 既存の床は上書きしない
            cell.tileEffect.active = true;
            cell.tileEffect.id = e.buff.empty() ? "fire" : e.buff;   // 床の種類（terrains.json。省略時は炎）
            cell.tileEffect.enemyOwned = true;        // 敵が置いたか
            cell.tileEffect.value = e.value;          // 踏んだ時の強さ
            cell.tileEffect.persistent = true;        // 踏んでも即消えない
            cell.tileEffect.hazardTurns = e.duration; // Nターンで自動消滅
        }
}
