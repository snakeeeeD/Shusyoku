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

// 効果：ブロック・バフ／デバフ
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

// ブロック（自分か、味方全員）
void Enemy::EffectBlock(ActionRun& r, const Effect& e)
{
    if (e.applyTo == ApplyTo::Allies)
    {
        int blk = m_buffManager.GetFinalBlock(e.value);
        for (auto ally : r.enemies)
            if (ally && ally->GetHp() > 0 && !ally->IsDying())
                ally->AddBlock(blk);   // 自分含む全味方に付与
    }
    else
        AddBlock(m_buffManager.GetFinalBlock(e.value));
}

// バフ・デバフ（自分／プレイヤー＝ダメージの後に付ける／味方全員）
void Enemy::EffectBuff(ActionRun& r, const Effect& e)
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
        if (r.hitPlayer && r.player) m_pendingPlayerBuffs.push_back(b);
    }
    else // Allies：全ての味方に付与（範囲無視・自分は除く）
    {
        for (auto other : r.enemies)
        {
            if (other == this || !other) continue;
            if (other->GetHp() <= 0 || other->IsDying()) continue;
            other->GetBuffManager().AddBuff(b);
        }
    }
}
