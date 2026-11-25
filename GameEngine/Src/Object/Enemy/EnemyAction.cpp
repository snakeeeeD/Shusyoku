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

// 行動の実行（流れと、効果の呼び分け）
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

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

    // 1回の行動の状態（狙う先・当たるか・与えたダメージ）。効果の関数（EnemyEffect*.cpp）に渡す
    ActionRun r{ act, act.target, playerCol, playerRow, mtC, mtR, gridMap, player, enemies, didAttack, moveTargetCol };
    DecideHit(r);
    ApproachTarget(r);                                   // 近づく・突進・並ぶ（EnemyMove.cpp）
    for (auto& e : act.effects) ApplyEffect(r, e);       // 効果を順に
    PlayActionFx(r);
    RecordAttackResult(r);
    NudgeIfNotMoved(r);
    return r.damage;
}

// 効果を1つ実行する（呼び分けだけ。中身は EnemyEffect*.cpp。新しい効果は、関数を1つ作ってここに1行足す）
void Enemy::ApplyEffect(ActionRun& r, const Effect& e)
{
    switch (e.kind)
    {
    case EffectKind::MoveToward:        EffectMoveToward(r, e); break;
    case EffectKind::Coil:              EffectCoil(r, e); break;
    case EffectKind::Lunge:             EffectLunge(r, e); break;
    case EffectKind::Retract:           EffectRetract(r, e); break;
    case EffectKind::MoveAway:          EffectMoveAway(r, e); break;
    case EffectKind::Damage:            EffectDamage(r, e); break;
    case EffectKind::Block:             EffectBlock(r, e); break;
    case EffectKind::Buff:
    case EffectKind::Debuff:            EffectBuff(r, e); break;
    case EffectKind::PullPlayer:        EffectPull(r, e); break;
    case EffectKind::KnockbackPlayer:   EffectKnockback(r, e); break;
    case EffectKind::Summon:            EffectSummon(r, e); break;
    case EffectKind::Hazard:            EffectHazard(r, e); break;
    case EffectKind::Heal:              EffectHeal(r, e); break;
    case EffectKind::AddCard:           EffectAddCard(r, e); break;
    }
}

// 動く前に「当たるか」を決める（＝予告と一致させる）
void Enemy::DecideHit(ActionRun& r)
{
    bool aimingDecoy = (r.moveTargetCol >= 0 && (r.mtC != r.playerCol || r.mtR != r.playerRow));
    r.hitPlayer = r.tg.unavoidable
        ? !aimingDecoy                                   // 必中：デコイを狙ってなければプレイヤーへ
        : IsThreateningCell(r.playerCol, r.playerRow, r.act);

    r.hitTarget = IsThreateningCell(r.mtC, r.mtR, r.act);
}

// 行動の演出：命中したら命中点に（hitVfx がある攻撃は BattleScene が1発ごとに出す）、自分に向けた演出は自分の位置に
void Enemy::PlayActionFx(ActionRun& r)
{
    // 命中したらエフェクトを命中点で再生（未指定ならスマッシュ）。hitVfx がある攻撃は、BattleScene が1発ごとに出す
    if (r.hitPlayer && r.player && r.act.hitVfx.empty())
    {
        std::string fx = r.act.vfx.empty() ? "smash" : r.act.vfx;
        EffectManager::Play(fx, r.player->worldX, r.player->worldY + 0.5f, r.player->worldZ);
    }

    // 自分に向けた演出（バフ・構えなど）は、当たったかに関係なく自分の位置で出す
    // 絵と同じ位置に合わせる（絵は drawZ だけ手前に描いている／高さは体の真ん中）
    if (!r.act.selfVfx.empty())
        EffectManager::Play(r.act.selfVfx, worldX, worldY + height * 0.5f, worldZ + m_drawZ);
}

// この行動がダメージ攻撃なら、命中したか(=避けられなかったか)を記録（外し続けた数）
void Enemy::RecordAttackResult(ActionRun& r)
{
    for (auto& e : r.act.effects)
        if (e.kind == EffectKind::Damage) { r.isAttack = true; break; }

    if (r.isAttack)
    {
        m_lastAttackWhiffed = !r.hitPlayer;
        if (r.hitPlayer) m_missStreak = 0;   // 当たったらリセット
        else           m_missStreak++;     // 外したら加算（ブロックのみは攻撃でないので不変）
    }
}

// 移動しようとしたのに0マスだった行動でも「行動した」のが分かるよう、小さくにじり寄る（逃げる行動は反対側へ）
void Enemy::NudgeIfNotMoved(ActionRun& r)
{
    bool movedThisAction = !m_movePath.empty() || m_didDash;
    bool wantsToward = (r.tg.approach == ApproachType::Toward || r.tg.approach == ApproachType::Dash);
    bool wantsAway = false;
    for (auto& e : r.act.effects)
    {
        if (e.kind == EffectKind::MoveToward) wantsToward = true;
        if (e.kind == EffectKind::MoveAway)   wantsAway = true;
    }
    if ((wantsToward || wantsAway) && !movedThisAction && !r.isAttack
        && !IsJumping() && !IsLunging() && !IsMoving())
    {
        float px = (r.playerCol - r.gridMap->GetCols() / 2.0f) * 1.1f;
        float pz = (r.playerRow - r.gridMap->GetRows() / 2.0f) * 1.1f;
        if (wantsAway) { px = worldX - (px - worldX); pz = worldZ - (pz - worldZ); }  // 逃げ行動は反対側へ
        StartLunge(px, pz, 0.28f);
    }
}
