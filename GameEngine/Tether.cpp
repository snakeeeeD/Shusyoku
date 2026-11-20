#include "Tether.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "tether"）を読む ----------
std::unique_ptr<EffectPartDef> TetherDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<TetherDef>();
    d->texture = j.value("texture", d->texture);
    d->tipTexture = j.value("tipTexture", d->tipTexture);
    d->extendTime = (std::max)(0.01f, j.value("extendTime", d->extendTime));   // 0秒だと割り算できないので下限
    d->maxHold = j.value("maxHold", d->maxHold);
    d->tileLen = (std::max)(0.05f, j.value("tileLen", d->tileLen));
    d->widthBase = j.value("widthBase", d->widthBase);
    d->widthTip = j.value("widthTip", d->widthTip);
    d->tipAspect = j.value("tipAspect", d->tipAspect);
    d->wave = j.value("wave", d->wave);
    d->waveFreq = j.value("waveFreq", d->waveFreq);
    d->waveSpeed = j.value("waveSpeed", d->waveSpeed);
    d->additive = j.value("additive", d->additive);
    if (j.contains("color"))
    {
        auto& c = j["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> TetherDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<Tether>(*this, at);
}

// ---------- 出す ----------
Tether::Tether(const TetherDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_tipTex = def.tipTexture.empty() ? nullptr : TextureManager::Get(def.tipTexture);
    m_to = XMFLOAT3(at.x, at.y, at.z);
    m_from = at.hasFrom ? at.from : m_to;   // 根元が無ければ長さ0（何も出ない）
    if (at.hasCam) m_fwd = at.camFwd;
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_scale = at.scale;
}

// ---------- 動かす ----------
void Tether::Update(float dt)
{
    m_elapsed += dt;
    if (m_releaseAt < 0.0f && m_elapsed > m_def.extendTime + m_def.maxHold) Stop(0.15f);   // 止め忘れ対策
}

void Tether::SetEnds(const XMFLOAT3& from, const XMFLOAT3& to) { m_from = from; m_to = to; }

void Tether::Stop(float fadeSec)
{
    if (m_releaseAt >= 0.0f) return;
    m_reachAtRelease = Reach();
    m_releaseAt = m_elapsed;
    m_retract = (fadeSec > 0.0f) ? fadeSec : 0.001f;
}

bool Tether::IsDone() const { return m_releaseAt >= 0.0f && m_elapsed - m_releaseAt >= m_retract; }

// 伸び具合：伸びる（イーズアウト）→ 1のまま → 戻る（だんだん速く縮む）
float Tether::Reach() const
{
    if (m_releaseAt >= 0.0f)
    {
        float u = (std::min)((m_elapsed - m_releaseAt) / m_retract, 1.0f);
        return m_reachAtRelease * (1.0f - u * u);
    }
    if (m_elapsed >= m_def.extendTime) return 1.0f;
    float u = m_elapsed / m_def.extendTime;
    return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
}

// ---------- 描く ----------
void Tether::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Overlay || !m_tex) return;
    XMVECTOR a = XMLoadFloat3(&m_from);
    XMVECTOR full = XMVectorSubtract(XMLoadFloat3(&m_to), a);
    float len = XMVectorGetX(XMVector3Length(full)) * Reach();
    if (len < 0.01f) return;
    XMVECTOR dir = XMVector3Normalize(full);
    XMVECTOR side = XMVector3Normalize(XMVector3Cross(dir, XMLoadFloat3(&m_fwd)));   // 画面の中で、線に直角
    int n = (std::max)(1, (int)(len / (m_def.tileLen * m_scale) + 0.5f));         // 区切りの数（長さに合わせて増える）
    auto pointAt = [&](float f) {   // 根元から f（0から1）の所。うねりは真ん中ほど大きく、両端は0
        float w = m_def.wave * m_scale * sinf(f * XM_PI) * sinf(f * m_def.waveFreq - m_elapsed * m_def.waveSpeed);
        XMFLOAT3 p; XMStoreFloat3(&p, XMVectorAdd(XMVectorAdd(a, XMVectorScale(dir, len * f)), XMVectorScale(side, w)));
        return p;
    };
    auto widthAt = [&](float f) { return (m_def.widthBase + (m_def.widthTip - m_def.widthBase) * f) * m_scale; };

    if (m_def.additive) r->SetAdditive(true);
    r->SetDepthEnabled(false);   // 体に食い込んでも隠れないように（flash と同じ）
    XMFLOAT3 p0 = pointAt(0.0f), prev = p0, p1 = p0;
    for (int i = 1; i <= n; i++)
    {
        p1 = pointAt((float)i / n);
        DrawSegment(r, m_tex, p0, p1, widthAt((float)(i - 1) / n), widthAt((float)i / n));
        prev = p0; p0 = p1;
    }
    if (m_tipTex)   // 先の絵：最後の区切りの向きにそろえ、先端に少し重ねて付ける
    {
        XMVECTOR d = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&p1), XMLoadFloat3(&prev)));
        float w = widthAt(1.0f), h = w * m_def.tipAspect;
        XMFLOAT3 s, e;
        XMStoreFloat3(&s, XMVectorSubtract(XMLoadFloat3(&p1), XMVectorScale(d, h * 0.3f)));
        XMStoreFloat3(&e, XMVectorAdd(XMLoadFloat3(&s), XMVectorScale(d, h)));
        DrawSegment(r, m_tipTex, s, e, w, w);
    }
    r->SetDepthEnabled(true);
    if (m_def.additive) r->SetAdditive(false);
}

// a から b へ、太さ wa → wb の帯を1枚（絵の上が b の側）
void Tether::DrawSegment(Renderer3D* r, ID3D11ShaderResourceView* tex, const XMFLOAT3& a, const XMFLOAT3& b, float wa, float wb)
{
    XMVECTOR d = XMVectorSubtract(XMLoadFloat3(&b), XMLoadFloat3(&a));
    XMFLOAT3 s; XMStoreFloat3(&s, XMVector3Normalize(XMVector3Cross(d, XMLoadFloat3(&m_fwd))));
    wa *= 0.5f; wb *= 0.5f;
    XMFLOAT3 tl(b.x - s.x * wb, b.y - s.y * wb, b.z - s.z * wb), tr(b.x + s.x * wb, b.y + s.y * wb, b.z + s.z * wb);
    XMFLOAT3 bl(a.x - s.x * wa, a.y - s.y * wa, a.z - s.z * wa), br(a.x + s.x * wa, a.y + s.y * wa, a.z + s.z * wa);
    r->DrawQuad(tex, tl, tr, bl, br, XMFLOAT4(0, 0, 1, 1), m_color);
}
