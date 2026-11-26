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

// 効果：移動（近づく・離れる・大蛇の巻きつき／首）

// 近づく（ボスの「＋移動」など）。予告の矢印があれば、その通りに動く（矢印と動き先をそろえる）
void Enemy::EffectMoveToward(ActionRun& r, const Effect& e)
{
    if (!m_plannedMovePath.empty()) MoveAlongPlanned(r.gridMap);
    else                            MoveToward(r.mtC, r.mtR, r.gridMap, e.value);
}

// 巻きつき（大蛇）
void Enemy::EffectCoil(ActionRun& r, const Effect& e)
{
    GrowCoil(r.gridMap, e.value + m_coilBonus, r.player);
}

// 首を伸ばす（大蛇）
void Enemy::EffectLunge(ActionRun& r, const Effect& e)
{
    LungeTo(r.gridMap);
}

// 首を戻す（大蛇）
void Enemy::EffectRetract(ActionRun& r, const Effect& e)
{
    RetractNeck(r.gridMap);
}

// 離れる（離脱後は接近の矢印が残らないよう消す）
void Enemy::EffectMoveAway(ActionRun& r, const Effect& e)
{
    MoveAway(r.mtC, r.mtR, r.gridMap, e.value);
    m_plannedMovePath.clear();   // 離脱後は接近矢印が残らないよう消す
}
