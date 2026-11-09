#include "Player.h"
#include "CardData.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include "EffectManager.h"
#include "FloatingText.h"
#include "ScreenShake.h"
#include "HitStop.h"
#include "RelicManager.h"
#include "Audio.h"

// シートは 4列 × (ANIM_FRAMES/4)行。コマの表示時間（各 ANIM_FRAMES コマ）
static const int   ANIM_FRAMES = 16;
static const int   SHEET_COLS = 4, SHEET_ROWS = ANIM_FRAMES / SHEET_COLS;
static const float ATK_DUR[ANIM_FRAMES]  = { 0.030f, 0.035f, 0.040f, 0.045f, 0.050f, 0.060f, 0.070f, 0.025f,      // 溜め長く
                                             0.020f, 0.020f, 0.025f, 0.040f, 0.060f, 0.050f, 0.045f, 0.060f };    // 振り下ろし一瞬→余韻
static const float SKL_DUR[ANIM_FRAMES]  = { 0.035f, 0.040f, 0.045f, 0.035f, 0.030f, 0.030f, 0.035f, 0.040f,
                                             0.050f, 0.060f, 0.070f, 0.060f, 0.045f, 0.040f, 0.035f, 0.050f };    // 最大の光で一拍止める
static const float HURT_DUR[ANIM_FRAMES] = { 0.025f, 0.030f, 0.035f, 0.040f, 0.040f, 0.040f, 0.035f, 0.035f,
                                             0.035f, 0.035f, 0.035f, 0.035f, 0.035f, 0.040f, 0.050f, 0.080f };    // 一瞬で反ってよろけて戻る
static const float IDLE_STEP = 0.10f;                                                                              // アイドル1コマ（ループ1.6秒）

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
    for (int i = 0; i < ANIM_FRAMES; i++) { if (t < dur[i]) return i; t -= dur[i]; }
    return -1;   // 再生終了
}

static const int ATK_READY_END = 7;   // 構え＝0～6コマ（6コマ目で止める）
static const int ATK_STRIKE    = 9;   // 使った瞬間はここから（ヒットストップの間このコマが見える）

static float FrameStart(const float* dur, int f) { float t = 0.0f; for (int i = 0; i < f; i++) t += dur[i]; return t; }

void Player::StartAttack() { m_anim = Anim::Attack; m_ready = false; m_animTime = FrameStart(ATK_DUR, ATK_STRIKE); }

void Player::FaceToward(int dc, int dr)
{
    if (dc == 0 && dr == 0) return;
    if (abs(dc) >= abs(dr)) SetFacing((dc > 0) ? 1 : -1, 0);
    else                    SetFacing(0, (dr > 0) ? 1 : -1);
}

void Player::SetReady(bool on)
{
    if (on == m_ready) return;
    m_ready = on;
    if (on) { m_anim = Anim::Attack; m_animTime = 0.0f; }                                                   // 振りかぶり始め
    else if (m_anim == Anim::Attack && m_animTime < FrameStart(ATK_DUR, ATK_STRIKE)) m_anim = Anim::Idle;   // 使わずにやめた
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
        if (m_ready && m_anim == Anim::Attack)                  // 構え：振りかぶった所で止めて待つ
        {
            float hold = FrameStart(ATK_DUR, ATK_READY_END) - 0.001f;
            if (m_animTime > hold) m_animTime = hold;
        }
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
    int af = ad.dur ? FrameAt(ad.dur, m_animTime) : (int)(m_idleTime / IDLE_STEP) % ANIM_FRAMES;

    float x = worldX, y = worldY, z = worldZ + 0.4f;
    float w = width, h = height;
    XMFLOAT4 uv = mirror ? XMFLOAT4(1, 0, -1, 1) : XMFLOAT4(0, 0, 1, 1);

    auto tex = TextureManager::Get(std::string(dir) + ad.suffix);
    if (tex && af >= 0)
    {
        w = width * 1.5f; h = height * 1.5f;                 // 正方形コマ＝立ち絵の1.5倍の範囲
        const float cw = 1.0f / SHEET_COLS, ch = 1.0f / SHEET_ROWS;   // 4列×(ANIM_FRAMES/4)行
        float u = (af % SHEET_COLS) * cw, v = (af / SHEET_COLS) * ch;
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
        FaceToward(dc, dr);
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

    // Vulnerable: 50%増
    if (m_buffManager.HasBuff(BuffType::Vulnerable))
        damage = damage * 150 / 100;
    if (int fr = m_buffManager.GetBuffValue(BuffType::Frenzy))
        damage = damage * (100 + 50 * fr) / 100;   // 狂乱：1枚ごとに被ダメ+50%
    if (m_buffManager.HasBuff(BuffType::Sanctuary)) damage /= 2;   // 聖域：受けるダメージ半分

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
    Se::Play(damage > 0 ? Se::PlayerHurt : Se::Shield);   // 通ったら被弾の音、全部防いだら盾の音

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
    {
        EffectManager::Play("heal", worldX, worldY + 0.5f, worldZ);   // 緑の十字
        FloatingTextManager::Spawn(
            worldX, worldY + height * 0.5f, worldZ,
            L"+" + std::to_wstring(healed),
            DirectX::XMFLOAT4(0.4f, 1.0f, 0.4f, 1.0f), 40.0f);   // 緑
    }
}

void Player::RestoreEnergy()
{
    m_energy = m_maxEnergy;
}

int Player::CostOf(const CardData& d) const
{
    return d.cost + (d.type == CardType::Move ? m_buffManager.GetBuffValue(BuffType::Mire) : 0);
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