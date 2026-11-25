#include "CardExecutor.h"
#include "CardEffect.h"
#include "CardDamage.h"
#include "EffectManager.h"
#include "BattleHighlighter.h"
#include "TerrainDataBase.h"
#include "RelicManager.h"
#include "CardDataBase.h"
#include "RangeShape.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "Se.h"
#include "Faith.h"
#include <algorithm>
#include <queue>
#include <map>

// カードの演出
// （CardExecutor を機能ごとに分けたファイル。Play＝1回のカード使用。CardExecutor.h）

// カードの演出（回転切り・扇・全体の爆発・隕石・単発は敵寄りに当たる数だけ）
void CardExecutor::Play::PlayCardVfx()
{
    XMFLOAT4 vcol(data.vfxColor[0], data.vfxColor[1], data.vfxColor[2], data.vfxColor[3]);
    for (const std::string& fx : data.vfx)
    {
        if (fx.empty()) continue;

        if (fx == "slash_spin")   // 回転切り：地面の斬撃の弧が周りを1周（敵のなぎ払いと同じ）＋周りに斬撃を6つ、少しずつずらして
        {
            float px = (playerCol - gridMap->GetCols() / 2.0f) * 1.1f;
            float pz = (playerRow - gridMap->GetRows() / 2.0f) * 1.1f;
            EffectManager::PlayAround("sweep", px, 0.5f, pz, data.range + 0.5f, (float)rand() / RAND_MAX * 6.2831853f, 0.0f, vcol);
            const int N = 6;
            for (int i = 0; i < N; i++)
            {
                float ang = 6.2831853f * i / N;
                EffectManager::Play("slash", px + cosf(ang) * 0.7f, 0.6f, pz + sinf(ang) * 0.7f, ang, i * 0.05f, vcol);
            }
        }
        else if (fx == "slash_fan")
        {
            int adx = 0, ady = 0;
            if (data.rangeType == RangeType::Cone)
                RangeShape::CardinalAim(playerCol, playerRow, targetCol, targetRow, adx, ady);
            float rot = atan2f(-(float)(targetRow - playerRow), (float)(targetCol - playerCol));
            for (int r = 0; r < gridMap->GetRows(); r++)
                for (int c = 0; c < gridMap->GetCols(); c++)
                {
                    if (c == playerCol && r == playerRow) continue;
                    if (!RangeShape::Contains(playerCol, playerRow, c, r, data.rangeType, data.range, 0, adx, ady)) continue;
                    float wx = (c - gridMap->GetCols() / 2.0f) * 1.1f;
                    float wz = (r - gridMap->GetRows() / 2.0f) * 1.1f;
                    EffectManager::Play("slash", wx, 0.6f, wz, rot, (float)rand() / RAND_MAX * 0.30f, vcol);
                }
        }
        else if (fx == "explosion_all")
        {
            float halfX = gridMap->GetCols() * 1.1f * 0.5f;
            float halfZ = gridMap->GetRows() * 1.1f * 0.5f;
            const int N = 24;                       // 爆発の数
            for (int i = 0; i < N; i++)
            {
                float x = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * halfX;
                float z = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * halfZ;
                float delay = (float)rand() / RAND_MAX * 0.8f;
                EffectManager::Play("explosion", x, 0.5f, z, 0.0f, delay, vcol);
            }
        }
        else if (fx == "meteor_rain")
        {
            float halfX = gridMap->GetCols() * 1.1f * 0.5f;
            float halfZ = gridMap->GetRows() * 1.1f * 0.5f;
            const int N = 24;                       // 降らせる隕石の数
            for (int i = 0; i < N; i++)
            {
                float x = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * halfX;
                float z = ((float)rand() / RAND_MAX - 0.5f) * 2.0f * halfZ;
                float delay = (float)rand() / RAND_MAX * 0.9f;   // バラけて降る
                EffectManager::Play("meteor_strike", x, 0.5f, z, 0.0f, delay, vcol);
            }
        }
        else   // 単発（敵寄り＋多段＋位置ランダム）
        {
            float vx = (targetCol - gridMap->GetCols() / 2.0f) * 1.1f;
            float vz = (targetRow - gridMap->GetRows() / 2.0f) * 1.1f;
            float px = (playerCol - gridMap->GetCols() / 2.0f) * 1.1f;
            float pz = (playerRow - gridMap->GetRows() / 2.0f) * 1.1f;
            float dx = px - vx, dz = pz - vz;
            float len = sqrtf(dx * dx + dz * dz);
            if (len > 1e-4f) { dx /= len; dz /= len; }
            float d = 0.20f;
            vx += dx * d; vz += dz * d;
            float rot = atan2f(-(float)(targetRow - playerRow), (float)(targetCol - playerCol));

            int hits = EffectiveHits(data, player); if (hits < 1) hits = 1;
            for (int i = 0; i < hits; i++)
            {
                float ox = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
                float oz = ((float)rand() / RAND_MAX - 0.5f) * 0.5f;
                EffectManager::Play(fx, vx + ox, 0.5f, vz + oz, rot, i * 0.08f, vcol);
            }
        }
    }
}
