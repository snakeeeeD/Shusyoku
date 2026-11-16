#include "ParticleBurst.h"
#include "EffectDataBase.h"   // FxMaterial・GetMaterial
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
    const float SCALE = 2.0f;            // 全体のエフェクト倍率（粒の大きさに掛ける）
    const float STREAK_MIN_LEN = 2.0f;   // 伸ばす粒は、止まりかけても幅のこの倍の長さを残す（欠片に見える）
    float Rand01() { return (float)rand() / RAND_MAX; }
    XMFLOAT4 Lerp(const XMFLOAT4& a, const XMFLOAT4& b, float t)
    {
        return XMFLOAT4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }
}

// ---------- データ：effects.json の "bursts" の1つを読む ----------
std::unique_ptr<EffectPartDef> BurstDef::Load(const nlohmann::json& b)
{
    auto d = std::make_unique<BurstDef>();
    auto color = [&](const char* key, XMFLOAT4 def)
    {
        if (!b.contains(key)) return def;
        auto& c = b[key];
        return XMFLOAT4(c[0], c[1], c[2], c[3]);
    };
    d->count = b.value("count", 10);
    d->speed = b.value("speed", 1.5f);
    d->life = b.value("life", 0.5f);
    d->scale = b.value("scale", 0.1f);
    d->gravity = b.value("gravity", 4.0f);
    d->drag = b.value("drag", 0.02f);
    d->colorStart = color("colorStart", d->colorStart);
    d->colorEnd = color("colorEnd", d->colorEnd);
    d->texture = b.value("texture", "");
    d->center = b.value("center", false);
    d->material = b.value("material", "");
    d->stretch = b.value("stretch", 0.0f);
    d->dragY = b.value("dragY", 0.0f);
    std::string spread = b.value("spread", "");
    d->spread = (spread == "screen") ? Spread::Screen : (spread == "forward") ? Spread::Forward : Spread::Sphere;
    d->cone = b.value("cone", d->cone);
    return d;
}

std::unique_ptr<EffectPart> BurstDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<ParticleBurst>(*this, at);
}

// ---------- 出す：粒をばらまく ----------
//  Sphere：球状（講義のCreateRingの分布を流用）＋やや上向き / Screen：画面に平行な円状（2Dのように、どの向きの線も同じ長さに見える）
//  Forward：出す向き（at.rot。水平に（sin, 0, cos)）を中心に、左右と上へ cone 度まで
ParticleBurst::ParticleBurst(const BurstDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture.empty() ? "particle" : def.texture);
    m_mat = def.material.empty() ? nullptr : EffectDataBase::GetMaterial(def.material);
    if (m_mat)
    {
        m_shape = TextureManager::Get(m_mat->shape);
        m_noise = TextureManager::Get(m_mat->noise);
        m_ramp = TextureManager::Get(m_mat->ramp);
    }
    const XMFLOAT3& rt = at.camRight;
    const XMFLOAT3& up = at.camUp;
    const float golden = XM_PI * (3.0f - sqrtf(5.0f));
    const float spin = Rand01() * XM_2PI;   // 毎回全体を回す（同じ向きに飛ぶのを避ける）
    m_particles.reserve(def.count);
    for (int i = 0; i < def.count; i++)
    {
        float s = def.speed * (0.5f + Rand01() * 0.5f);
        Particle p;
        p.pos = p.origin = XMFLOAT3(at.x, at.y, at.z);
        if (def.spread == Spread::Screen)
        {
            float a = XM_2PI * (i + Rand01() * 0.8f) / def.count + spin;   // 均等に配って、少しずらす
            float ca = cosf(a) * s, sa = sinf(a) * s;
            p.vel = XMFLOAT3(rt.x * ca + up.x * sa, rt.y * ca + up.y * sa, rt.z * ca + up.z * sa);
        }
        else if (def.spread == Spread::Forward)
        {
            float yaw = at.rot + XMConvertToRadians(def.cone) * (Rand01() * 2.0f - 1.0f);   // 左右にずらす
            float el = XMConvertToRadians(def.cone) * Rand01();                             // 上へ持ち上げる
            p.vel = XMFLOAT3(sinf(yaw) * cosf(el) * s, sinf(el) * s, cosf(yaw) * cosf(el) * s);
        }
        else
        {
            float tt = (def.count > 1) ? (float)i / (def.count - 1) : 0.5f;
            float phi = acosf(1.0f - 2.0f * tt);
            float theta = golden * i + spin;
            float sp = sinf(phi);
            p.vel = XMFLOAT3(sp * cosf(theta) * s,
                cosf(phi) * s * 0.6f + def.speed * 0.5f,   // やや上向き
                sp * sinf(theta) * s);
        }
        p.lifeMax = def.life * (0.7f + Rand01() * 0.6f);
        p.life = p.lifeMax;
        p.seed = Rand01();
        m_particles.push_back(p);
    }
}

// ---------- 動かす ----------
void ParticleBurst::Update(float dt)
{
    m_time += dt;
    float d = 1.0f - m_def.drag;
    for (auto& p : m_particles)
    {
        p.life -= dt;
        p.vel.y -= m_def.gravity * dt;
        p.vel.x *= d; p.vel.z *= d;          // 空気の抵抗（横）
        p.vel.y *= 1.0f - m_def.dragY;       // 縦の空気抵抗（既定0＝今まで通り）
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;
    }
    m_particles.erase(std::remove_if(m_particles.begin(), m_particles.end(),
        [](const Particle& p) { return p.life <= 0.0f; }), m_particles.end());
}

// ---------- 描く ----------
void ParticleBurst::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Overlay) return;   // 粒は上の順番（キャラの後）でだけ描く
    const float sc = m_def.scale * SCALE;
    const float pitch = XMConvertToRadians(Renderer3D::GetBillboardPitch());
    if (m_mat) r->SetAdditive(m_mat->additive);
    for (auto& p : m_particles)
    {
        float t = 1.0f - p.life / p.lifeMax;   // 寿命の進み具合（0→1）
        XMFLOAT4 c = Lerp(m_def.colorStart, m_def.colorEnd, t);
        float px = p.pos.x, py = p.pos.y, pz = p.pos.z;
        if (m_def.center)   // 発生点に絵の中心が来るよう、上方向に半分下げる
        {
            py -= 0.5f * sc * cosf(pitch);
            pz -= 0.5f * sc * sinf(pitch);
        }
        if (m_mat)   // 材質のある粒：エフェクト用シェーダーで描く（形・ノイズ・色の帯）
        {
            FxParams fx;
            fx.life = t; fx.erodeStart = m_mat->erodeStart; fx.erodeEnd = m_mat->erodeEnd;
            fx.glow = m_mat->glow; fx.scroll = m_mat->scroll; fx.noiseScale = m_mat->noiseScale;
            fx.time = m_time; fx.seed = p.seed;
            fx.appear = m_mat->appear;
            r->DrawFxBillboard(m_shape, m_noise, m_ramp, fx, px, py, pz, sc, c);
            continue;
        }
        if (m_def.stretch > 0.0f)   // 速さの向きに伸ばす粒：速い間は長い線、遅くなると短い欠片
        {
            float spd = sqrtf(p.vel.x * p.vel.x + p.vel.y * p.vel.y + p.vel.z * p.vel.z);
            float ox = p.pos.x - p.origin.x, oy = p.pos.y - p.origin.y, oz = p.pos.z - p.origin.z;
            float flown = sqrtf(ox * ox + oy * oy + oz * oz);                            // 出てから進んだ距離
            float len = fmaxf(fminf(spd * m_def.stretch, flown), sc * STREAK_MIN_LEN);   // 出た所より後ろへは伸ばさない
            r->DrawStreak(m_tex, p.pos, p.vel, len, sc, c);
            continue;
        }
        r->DrawBillboard(m_tex, px, py, pz, sc, sc, 0.0f, c);
    }
    if (m_mat) r->SetAdditive(false);
}
