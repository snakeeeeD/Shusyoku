#include "Player.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "RelicManager.h"
#include "Audio.h"

// コマの表示時間（各8コマ）
static const float ATK_DUR[8] = { 0.04f, 0.06f, 0.08f, 0.12f, 0.03f, 0.03f, 0.10f, 0.10f };   // 溜め長く・振り下ろし一瞬
static const float SKL_DUR[8] = { 0.05f, 0.06f, 0.07f, 0.06f, 0.06f, 0.07f, 0.16f, 0.10f };   // 最大の光で一拍止める
static const float HURT_DUR[8] = { 0.05f, 0.07f, 0.07f, 0.07f, 0.07f, 0.07f, 0.06f, 0.06f };   // 被弾：一瞬で反って戻る
static const float IDLE_STEP = 0.16f;                                                           // アイドル1コマ（ループ）

// アニメの種類ごとの設定（Player::Anim と同じ順番。dur が nullptr ＝ループ）
struct AnimDef { const char* suffix; const float* dur; };
static const AnimDef ANIMS[] = {
    { "_idle",   nullptr  },   // Idle
    { "_attack", ATK_DUR  },   // Attack
    { "_skill",  SKL_DUR  },   // Skill
    { "_hurt",   HURT_DUR },   // Hurt
};

static int FrameAt(const float* dur, float t)
{
    for (int i = 0; i < 8; i++) { if (t < dur[i]) return i; t -= dur[i]; }
    return -1;   // 再生終了
}

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
    // アニメの進行（ループ以外は終わったらアイドルへ戻る）
    m_idleTime += deltaTime;
    const AnimDef& ad = ANIMS[(int)m_anim];
    if (ad.dur)
    {
        m_animTime += deltaTime;
        if (FrameAt(ad.dur, m_animTime) < 0) m_anim = Anim::Idle;
    }

    // 移動の差分から向きを更新（動いている間だけ）
    float dx = worldX - m_prevX, dz = worldZ - m_prevZ;
    if (fabsf(dx) > 0.0015f || fabsf(dz) > 0.0015f)
    {
        if (fabsf(dx) >= fabsf(dz)) { m_faceDx = (dx > 0.0f) ? 1 : -1; m_faceDz = 0; }
        else { m_faceDx = 0; m_faceDz = (dz > 0.0f) ? 1 : -1; }
    }
    m_prevX = worldX; m_prevZ = worldZ;
}

void Player::Draw3D(Renderer3D* renderer)
{
    if (!isActive) return;

    // 向き → シート名の頭
    const char* dir; bool mirror = false;
    if (m_faceDx != 0) { dir = "player_side";  mirror = (m_faceDx < 0); }
    else if (m_faceDz > 0) { dir = "player_front"; }
    else { dir = "player_back"; }

    // 再生するアニメと今のコマ
    const AnimDef& ad = ANIMS[(int)m_anim];
    int af = ad.dur ? FrameAt(ad.dur, m_animTime) : (int)(m_idleTime / IDLE_STEP) % 8;

    float x = worldX, y = worldY, z = worldZ + 0.4f;
    float w = width, h = height;
    XMFLOAT4 uv = mirror ? XMFLOAT4(1, 0, -1, 1) : XMFLOAT4(0, 0, 1, 1);

    auto tex = TextureManager::Get(std::string(dir) + ad.suffix);
    if (tex && af >= 0)
    {
        w = width * 1.5f; h = height * 1.5f;                 // 正方形コマ＝立ち絵の1.5倍の範囲
        const float cw = 1.0f / 4.0f, ch = 1.0f / 2.0f;     // 4列×2行
        float u = (af % 4) * cw, v = (af / 4) * ch;
        uv = mirror ? XMFLOAT4(u + cw, v, -cw, ch) : XMFLOAT4(u, v, cw, ch);

        // コマ下の余白(1/6)ぶん基点を下げて、足元を立ち絵に揃える
        float pad = h / 6.0f;
        float pitch = XMConvertToRadians(Renderer3D::GetBillboardPitch());
        x -= pad * sinf(pitch) * sinf(m_BillboardRotation);
        y -= pad * cosf(pitch);
        z -= pad * sinf(pitch) * cosf(m_BillboardRotation);
    }
    else
    {
        tex = TextureManager::Get(dir);                  // シートが無ければ立ち絵
        if (!tex) tex = TextureManager::Get("player");
    }
    renderer->DrawBillboard(tex, x, y, z, w, h, m_BillboardRotation, GetDrawColor(), uv);
}

// カードが実際に使われた時：攻撃は対象の方を向いて振る、スキル／パワーは力を溜める
void Player::OnCardUsed(CardType type, int dc, int dr)
{
    if (type == CardType::Attack)
    {
        if (dc != 0 || dr != 0)
        {
            if (abs(dc) >= abs(dr)) SetFacing((dc > 0) ? 1 : -1, 0);
            else                    SetFacing(0, (dr > 0) ? 1 : -1);
        }
        StartAttack();
    }
    else if (type == CardType::Skill || type == CardType::Power)
        StartSkill();
}

void Player::TakeDamage(int damage, DamageFeel feel)
{
    if (feel == DamageFeel::Poison)
    {
        m_hp -= damage;
        if (m_hp < 0) m_hp = 0;
        DamageFeedback::Play(feel, worldX, worldY + height * 0.5f, worldZ, damage, 0);
        return;
    }

    Audio::PlaySE("Assets/Sound/se/shield.mp3");

    // Vulnerable: 50%増
    if (m_buffManager.HasBuff(BuffType::Vulnerable))
        damage = damage * 150 / 100;
    if (int fr = m_buffManager.GetBuffValue(BuffType::Frenzy))
        damage = damage * (100 + 50 * fr) / 100;   // 狂乱：1枚ごとに被ダメ+50%

    // ブロックで先に受ける
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
        StartHurt();
        ScreenShake::Add(ScreenShake::PowerForDamage(damage));
        HitStop::Add(0.05f);
    }
    DamageFeedback::Play(feel, worldX, worldY + height * 0.5f, worldZ, damage, blocked);
}

void Player::Heal(int amount)
{
    int before = m_hp;
    m_hp = min(m_hp + amount, m_maxHp);
    int healed = m_hp - before;                 // 実際に回復した量（オーバーヒール除外）
    if (healed > 0)
        FloatingTextManager::Spawn(
            worldX, worldY + height * 0.5f, worldZ,
            L"+" + std::to_wstring(healed),
            DirectX::XMFLOAT4(0.4f, 1.0f, 0.4f, 1.0f), 40.0f);   // 緑
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
    if (amount > 0)
    {
        Audio::PlaySE("Assets/Sound/se/shield.mp3");
        EffectManager::Play("defend", worldX, worldY + 0.4f, worldZ);
    }
    m_block += amount;
}

int Player::GetMoveRange(int baseRange) const
{
    return m_buffManager.GetFinalMoveRange(baseRange) + RelicManager::SumValue("moveRange");
}