#include "Projectile.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "projectile"）を読む ----------
std::unique_ptr<EffectPartDef> ProjectileDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<ProjectileDef>();
    auto color = [&](const char* key, XMFLOAT4& out) {
        if (!j.contains(key)) return;
        auto& c = j[key];
        out = XMFLOAT4(c[0], c[1], c[2], c[3]);
    };
    d->texture = j.value("texture", d->texture);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->size = j.value("size", d->size);
    d->aspect = j.value("aspect", d->aspect);
    d->faceTravel = j.value("faceTravel", d->faceTravel);
    d->spin = j.value("spin", d->spin);
    d->arc = j.value("arc", d->arc);
    d->trailTexture = j.value("trailTexture", d->trailTexture);
    d->trailWidth = j.value("trailWidth", d->trailWidth);
    d->trailLen = j.value("trailLen", d->trailLen);
    color("color", d->color);
    color("trailColor", d->trailColor);
    return d;
}

std::unique_ptr<EffectPart> ProjectileDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<Projectile>(*this, at);
}

// ---------- 出す ----------
Projectile::Projectile(const ProjectileDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_trailTex = TextureManager::Get(def.trailTexture);
    m_to = XMFLOAT3(at.x, at.y, at.z);
    m_from = at.hasFrom ? at.from : m_to;   // 飛び始めが無ければ、その場（飛ばない）
    float dx = m_to.x - m_from.x, dy = m_to.y - m_from.y, dz = m_to.z - m_from.z;
    m_arcH = sqrtf(dx * dx + dy * dy + dz * dz) * def.arc;
    if (at.hasCam) { m_right = at.camRight; m_up = at.camUp; m_fwd = at.camFwd; }
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_trailColor = XMFLOAT4(def.trailColor.x * at.tint.x, def.trailColor.y * at.tint.y, def.trailColor.z * at.tint.z, def.trailColor.w * at.tint.w);
    m_scale = at.scale;
}

// 飛んだ割合 u の位置：まっすぐ進む＋上へ山なり（真ん中で m_arcH）
XMFLOAT3 Projectile::PosAt(float u) const
{
    float lift = m_arcH * 4.0f * u * (1.0f - u);
    return XMFLOAT3(m_from.x + (m_to.x - m_from.x) * u,
                    m_from.y + (m_to.y - m_from.y) * u + lift,
                    m_from.z + (m_to.z - m_from.z) * u);
}

// ---------- 描く ----------
void Projectile::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Overlay) return;
    if (m_elapsed < 0.0f) return;
    float u = (std::min)(m_elapsed / m_def.life, 1.0f);
    DrawTrail(r, u);
    DrawHead(r, u);
}

// 軌跡：頭から後ろへ「距離×trailLen」ぶん。短い帯をつないで描き、後ろほど細く薄く
void Projectile::DrawTrail(Renderer3D* r, float u)
{
    if (m_def.trailWidth <= 0.0f || !m_trailTex) return;
    const int SEG = 16;                                      // 帯の分け数（多いほど弧がなめらか）
    float u0 = (std::max)(0.0f, u - m_def.trailLen);
    if (u - u0 < 0.001f) return;
    auto widthAt = [&](float f) { return m_def.trailWidth * m_scale * (0.25f + 0.75f * f); };   // f：0=尾 1=頭
    XMFLOAT3 a = PosAt(u0);
    for (int i = 1; i <= SEG; i++)
    {
        float fa = (float)(i - 1) / SEG, fb = (float)i / SEG;
        XMFLOAT3 b = PosAt(u0 + (u - u0) * fb);
        // 帯の横向き＝進む向きとカメラの前の両方に直角（画面の中で、線に直角）
        XMVECTOR dir = XMVectorSubtract(XMLoadFloat3(&b), XMLoadFloat3(&a));
        XMVECTOR side = XMVector3Normalize(XMVector3Cross(dir, XMLoadFloat3(&m_fwd)));
        XMFLOAT3 s; XMStoreFloat3(&s, side);
        float wa = widthAt(fa) * 0.5f, wb = widthAt(fb) * 0.5f;
        XMFLOAT3 tl(b.x - s.x * wb, b.y - s.y * wb, b.z - s.z * wb), tr(b.x + s.x * wb, b.y + s.y * wb, b.z + s.z * wb);
        XMFLOAT3 bl(a.x - s.x * wa, a.y - s.y * wa, a.z - s.z * wa), br(a.x + s.x * wa, a.y + s.y * wa, a.z + s.z * wa);
        XMFLOAT4 c = m_trailColor; c.w *= fb;               // 頭ほど濃い
        r->DrawQuad(m_trailTex, tl, tr, bl, br, XMFLOAT4(0, 0, 1, 1), c);
        a = b;
    }
}

// 頭の絵：faceTravel なら絵の上を進む向き（画面の中）へ、そうでなければ spin で回す
void Projectile::DrawHead(Renderer3D* r, float u)
{
    if (!m_tex) return;
    float vx, vy;   // 絵の上が向く画面の中の向き（右, 上）
    if (m_def.faceTravel)
    {
        // 進む向き＝位置を u で微分したもの（まっすぐ＋弧の傾き）
        float k = m_arcH * 4.0f * (1.0f - 2.0f * u);
        XMFLOAT3 d(m_to.x - m_from.x, m_to.y - m_from.y + k, m_to.z - m_from.z);
        vx = d.x * m_right.x + d.y * m_right.y + d.z * m_right.z;
        vy = d.x * m_up.x + d.y * m_up.y + d.z * m_up.z;
        float len = sqrtf(vx * vx + vy * vy);
        if (len < 0.0001f) { vx = 0.0f; vy = 1.0f; } else { vx /= len; vy /= len; }
    }
    else
    {
        float a = XMConvertToRadians(m_def.spin * m_elapsed);
        vx = -sinf(a); vy = cosf(a);
    }
    // 絵の上（U）と右（R）をワールドで
    XMFLOAT3 U(m_right.x * vx + m_up.x * vy, m_right.y * vx + m_up.y * vy, m_right.z * vx + m_up.z * vy);
    XMFLOAT3 R(m_right.x * vy - m_up.x * vx, m_right.y * vy - m_up.y * vx, m_right.z * vy - m_up.z * vx);
    XMFLOAT3 p = PosAt(u);
    float hw = m_def.size * m_scale * 0.5f, hh = hw * m_def.aspect;
    auto at = [&](float sx, float sy) {
        return XMFLOAT3(p.x + R.x * hw * sx + U.x * hh * sy, p.y + R.y * hw * sx + U.y * hh * sy, p.z + R.z * hw * sx + U.z * hh * sy);
    };
    r->DrawQuad(m_tex, at(-1, 1), at(1, 1), at(-1, -1), at(1, -1), XMFLOAT4(0, 0, 1, 1), m_color);
}
