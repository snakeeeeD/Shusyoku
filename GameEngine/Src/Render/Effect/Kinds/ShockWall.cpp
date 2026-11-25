#include "ShockWall.h"
#include "EffectManager.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

namespace
{
    const float BASE_Y = 0.02f;   // 根もとを地面から少し浮かせる（地面とちらつかないように）
}

// ---------- データ：effects.json の "parts" の1つ（"type": "wall"）を読む ----------
std::unique_ptr<EffectPartDef> ShockWallDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<ShockWallDef>();
    d->texture = j.value("texture", d->texture);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->radiusStart = j.value("radiusStart", d->radiusStart);
    d->radiusEnd = j.value("radiusEnd", d->radiusEnd);
    d->height = j.value("height", d->height);
    d->slant = j.value("slant", d->slant);
    d->ease = j.value("ease", d->ease);
    d->spin = j.value("spin", d->spin);
    d->segments = (std::max)(3, j.value("segments", d->segments));
    d->additive = j.value("additive", d->additive);
    d->breakAt = j.value("breakAt", d->breakAt);
    d->breakFx = j.value("breakFx", d->breakFx);
    d->breakCount = j.value("breakCount", d->breakCount);
    d->breakHeight = j.value("breakHeight", d->breakHeight);
    if (j.contains("color"))
    {
        auto& c = j["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> ShockWallDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<ShockWall>(*this, at);
}

// ---------- 出す ----------
ShockWall::ShockWall(const ShockWallDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_x = at.x; m_z = at.z;   // 高さは使わず、出した所の真下の地面から立てる
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_scale = at.scale;
}

void ShockWall::Shape(float t, float& radius, float& height) const
{
    float grow = 1.0f - powf(1.0f - t, m_def.ease);   // 最初に一気に広がって、だんだんゆっくり（輪と同じイーズアウト）
    radius = (m_def.radiusStart + (m_def.radiusEnd - m_def.radiusStart) * grow) * m_scale;
    height = m_def.height * grow;
}

// ---------- 動かす ----------
void ShockWall::Update(float dt)
{
    m_elapsed += dt;
    if (!m_broken && m_elapsed >= m_def.breakAt * m_def.life)
    {
        m_broken = true;
        Break();
    }
}

void ShockWall::Break()
{
    if (m_def.breakFx.empty()) return;
    float radius, height;
    Shape((std::min)(m_elapsed / m_def.life, 1.0f), radius, height);
    float y = height * m_def.breakHeight;
    float r = radius + m_def.slant * y;                 // 傾いているので、高い所ほど外
    int count = (std::max)(1, (int)(m_def.breakCount * m_scale + 0.5f));   // 大きい壁ほど縁が長いので、破片を増やす
    for (int k = 0; k < count; k++)
    {
        float a = XM_2PI * k / count + m_def.spin * m_elapsed;
        float ca = cosf(a), sa = sinf(a);
        // 外向きに出す。粒の "forward" は（sin rot, 0, cos rot）の向きへ飛ぶので、その向きが外（cos a, 0, sin a）になる rot を渡す
        EffectManager::Play(m_def.breakFx, m_x + ca * r, BASE_Y + y, m_z + sa * r, atan2f(ca, sa));
    }
}

// ---------- 描く ----------
void ShockWall::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Ground && layer != EffectLayer::Overlay) return;   // 地面（奥半分）と上（手前半分）でだけ描く
    float t = (std::min)(m_elapsed / m_def.life, 1.0f);   // 寿命の進み具合（0→1）
    float radius, height;
    Shape(t, radius, height);
    float fade = (t < m_def.breakAt) ? 1.0f
        : 1.0f - (t - m_def.breakAt) / (std::max)(1.0f - m_def.breakAt, 0.01f);   // 割れた後は急に薄くなる
    XMFLOAT4 c = m_color;
    c.w *= fade;
    float top = radius + m_def.slant * height;          // 上の縁の半径（外へ傾ける）
    float spin = m_def.spin * m_elapsed;
    int n = m_def.segments;

    // カメラの側の半分＝手前。奥は地面の順番で描いてキャラに隠し、手前は上の順番で描いてキャラの前に出す
    XMFLOAT3 eye;
    XMStoreFloat3(&eye, XMMatrixInverse(nullptr, r->GetViewMatrix()).r[3]);   // カメラの位置
    float toCamX = eye.x - m_x, toCamZ = eye.z - m_z;                         // 中心からカメラへの向き（水平）
    bool drawFront = (layer == EffectLayer::Overlay);

    if (m_def.additive) r->SetAdditive(true);
    for (int i = 0; i < n; i++)   // 縦長の台形を輪の形に並べて円柱にする。画像は一周でちょうど1枚
    {
        float a0 = XM_2PI * i / n + spin, a1 = XM_2PI * (i + 1) / n + spin;
        float mid = (a0 + a1) * 0.5f;
        bool isFront = cosf(mid) * toCamX + sinf(mid) * toCamZ > 0.0f;   // この板がカメラの側にあるか
        if (isFront != drawFront) continue;
        float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
        XMFLOAT3 tl(m_x + c0 * top, BASE_Y + height, m_z + s0 * top);
        XMFLOAT3 tr(m_x + c1 * top, BASE_Y + height, m_z + s1 * top);
        XMFLOAT3 bl(m_x + c0 * radius, BASE_Y, m_z + s0 * radius);
        XMFLOAT3 br(m_x + c1 * radius, BASE_Y, m_z + s1 * radius);
        r->DrawQuad(m_tex, tl, tr, bl, br, XMFLOAT4((float)i / n, 0.0f, (float)(i + 1) / n, 1.0f), c);
    }
    if (m_def.additive) r->SetAdditive(false);
}
