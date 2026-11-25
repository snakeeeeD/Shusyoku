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

// 体力（ダメージ・ブロック・無敵）と、倒れる演出（塵化）
// （Enemy のメンバー。Enemy.cpp から機能ごとに分けたファイル）

// 倒れていく途中：とどめの瞬間は白く光って少し大きく、その後ノイズに沿って焼けるように溶ける（Dissolve_PS）
void Enemy::DrawDeath(Renderer3D* renderer)
{
    const FxMaterial* mat = EffectDataBase::GetMaterial("fx_dissolve");   // ノイズ・色の帯・細かさ・明るさ
    DissolveParams dp;
    float s = 1.0f;
    if (m_deathTimer < DEATH_FLASH) { dp.white = 0.85f; s = 1.04f; }
    else dp.progress = (std::min)(1.0f, (m_deathTimer - DEATH_FLASH) / DEATH_DISSOLVE);
    dp.seed = m_deathSeed;
    XMFLOAT4 c = GetDrawColor();
    float w = width * s, h = height * s;
    if (!mat)   // 材質が無ければ、前と同じく薄くなって消える
    {
        c.w *= 1.0f - dp.progress;
        renderer->DrawBillboard(TextureManager::Get(m_textureName), worldX, worldY, worldZ + m_drawZ, w, h, 0.0f, c, FacingUV());
        return;
    }
    dp.noiseScale = mat->noiseScale; dp.glow = mat->glow;
    ID3D11ShaderResourceView* noise = TextureManager::Get(mat->noise);
    ID3D11ShaderResourceView* ramp = TextureManager::Get(mat->ramp);
    renderer->DrawDissolveBillboard(TextureManager::Get(m_textureName), noise, ramp, dp,
        worldX, worldY, worldZ + m_drawZ, w, h, 0.0f, c, FacingUV());
    for (auto& o : m_overlays)   // 重ねる絵も同じ模様で一緒に溶かす
        renderer->DrawDissolveBillboard(TextureManager::Get(o), noise, ramp, dp,
            worldX, worldY, worldZ + m_drawZ, w, h, 0.0f, c, FacingUV());
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

void Enemy::StartDeath()
{
    if (m_dying) return;
    m_dying = true;
    m_deathTimer = 0.0f;
    m_emberTimer = 0.0f;
    m_deathSeed = (float)rand() / RAND_MAX * 10.0f;
    HitStop::Add(0.1f);   // とどめは、ふつうの命中（0.05秒）より長く止める
}

void Enemy::UpdateDeath(float deltaTime)
{
    if (!m_dying) return;
    m_deathTimer += deltaTime;

    // 溶けている間、溶けているあたりから火の粉を出す（上の方から先に溶けるので、出す高さも上から下へ）
    const float EMBER_STEP = 0.03f;   // 火の粉を出す間隔（秒）
    float p = (m_deathTimer - DEATH_FLASH) / DEATH_DISSOLVE;
    if (p <= 0.0f || p >= 0.95f) return;
    m_emberTimer -= deltaTime;
    while (m_emberTimer <= 0.0f)
    {
        m_emberTimer += EMBER_STEP;
        float lx = ((float)rand() / RAND_MAX - 0.5f) * 0.7f;                  // 絵の横幅の真ん中7割
        float ly = 1.0f - p - ((float)rand() / RAND_MAX - 0.3f) * 0.5f;       // 今溶けているあたりの高さ（1＝頭、0＝足）
        ly = (std::max)(0.0f, (std::min)(1.0f, ly));
        XMFLOAT3 p = SpritePoint(lx, ly);
        EffectManager::Play("death_ember", p.x, p.y, p.z);
    }
}
