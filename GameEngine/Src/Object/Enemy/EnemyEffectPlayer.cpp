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

// 効果：プレイヤーを引き寄せる・吹き飛ばす

// プレイヤーを引き寄せる（当たった時だけ）
void Enemy::EffectPull(ActionRun& r, const Effect& e)
{
    if (r.hitPlayer) PullPlayer(r.playerCol, r.playerRow, r.gridMap, r.player, e.value);
}

// プレイヤーを吹き飛ばす（当たった時だけ）
void Enemy::EffectKnockback(ActionRun& r, const Effect& e)
{
    if (r.hitPlayer) KnockbackPlayer(r.playerCol, r.playerRow, r.gridMap, r.player, e.value);
}
