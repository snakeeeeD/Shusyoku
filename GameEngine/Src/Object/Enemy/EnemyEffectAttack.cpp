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

// 効果：ダメージ・吸血・カードを入れる

// ダメージ（当たった時だけ。連撃は回数も覚える）。跳ねる動きもここ
void Enemy::EffectDamage(ActionRun& r, const Effect& e)
{
    if (r.hitTarget && r.didAttack) *r.didAttack = true;   // 標的が射程内＝実際に攻撃した時だけ
    if (r.tg.approach != ApproachType::Dash && !r.tg.unavoidable)
    {
        if (r.tg.rangeType == RangeType::Area) StartJump(1.5f, 0.4f);
        else                                 StartJump(0.5f, 0.25f);
    }
    if (r.hitPlayer)
    {
        int perHit = m_buffManager.GetFinalAttack(e.value);
        int h = (e.hits > 1) ? e.hits : 1;
        r.damage += perHit * h;
        m_lastHitDamage = perHit;   // 1ヒット分
        m_lastHitCount = h;        // ヒット数
    }
}

// 回復（value>0＝決まった量、0＝与えたダメージ分を吸う）。命中した時だけ
void Enemy::EffectHeal(ActionRun& r, const Effect& e)
{
    int heal = (e.value > 0) ? e.value : r.damage;   // value>0=固定回復 / 0=与ダメージ分を吸収
    if (heal > 0 && r.hitPlayer)                       // 命中時だけ吸血
    {
        m_HP = min(m_maxHP, m_HP + heal);
        EffectManager::Play("heal", worldX, worldY + 0.5f, worldZ);
        FloatingTextManager::Spawn(worldX, worldY + 0.9f, worldZ,
            L"+" + std::to_wstring(heal),
            XMFLOAT4(0.4f, 1.0f, 0.5f, 1.0f), 32.0f);   // 緑で回復量
    }
}

// 手札・山札・捨て札にカードを入れる（範囲の攻撃は当たった時だけ、範囲なしはいつも）
void Enemy::EffectAddCard(ActionRun& r, const Effect& e)
{
    if (r.hitPlayer || r.tg.rangeType == RangeType::None)   // 範囲系は命中時だけ／None(必中的)は常時
        m_pendingCurses.push_back({ e.summonId, e.buff.empty() ? "deck" : e.buff, e.value > 0 ? e.value : 1 });
}
