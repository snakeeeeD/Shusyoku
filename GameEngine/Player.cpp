#include "Player.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "RelicManager.h"
#include "Audio.h"

Player::Player()
    : m_hp(50), m_maxHp(50)
    , m_energy(5), m_maxEnergy(5)
    , m_block(0)
{
    width = 1.0f;
    height = 1.0f;
    worldY = 0.0f;
    m_BillboardRotation = 0.0f;
    m_displayHp = (float)m_maxHp;

}

void Player::Update(float deltaTime)
{
}

void Player::Draw3D(Renderer3D* renderer)
{
    if (!isActive) return;
    renderer->DrawBillboard(
        TextureManager::Get("player"),
        worldX, worldY, worldZ,
        width, height, m_BillboardRotation, GetDrawColor()
    );
}

void Player::TakeDamage(int damage, DamageFeel feel)
{
    Audio::PlaySE("Assets/Sound/se/shield.mp3");

    // Vulnerable: 50%‘
    if (m_buffManager.HasBuff(BuffType::Vulnerable))
        damage = damage * 150 / 100;

    // ƒuƒƒbƒN‚Åæ‚ÉŽó‚¯‚é
    int blocked = 0;
    if (m_block > 0)
    {
        blocked = min(m_block, damage);
        m_block -= blocked;
        damage -= blocked;
    }
    m_hp -= damage;
    if (m_hp < 0) m_hp = 0;

    if (damage > 0 && feel == DamageFeel::Hit)
    {
        StartHitFlash();
        ScreenShake::Add(ScreenShake::PowerForDamage(damage));
    }
    DamageFeedback::Play(feel, worldX, worldY + height * 0.5f, worldZ, damage, blocked);
}

void Player::RestoreEnergy()
{
    m_energy = m_maxEnergy;
}

bool Player::UseEnergy(int cost)
{
    if (m_energy < cost) return false;
    m_energy -= cost;
    return true;
}

void Player::AddBlock(int amount)
{
    if (amount > 0) Audio::PlaySE("Assets/Sound/se/shield.mp3");
    m_block += amount;
}

int Player::GetMoveRange(int baseRange) const
{
    return m_buffManager.GetFinalMoveRange(baseRange) + RelicManager::SumValue("moveRange");
}