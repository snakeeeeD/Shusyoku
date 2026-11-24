#include "Helix.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "helix"）を読む ----------
std::unique_ptr<EffectPartDef> HelixDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<HelixDef>();
    auto col = [&](const char* key, XMFLOAT4& c) { if (j.contains(key)) { auto& v = j[key]; c = XMFLOAT4(v[0], v[1], v[2], v[3]); } };
    d->texture = j.value("texture", d->texture);
    d->coreTexture = j.value("coreTexture", d->coreTexture);
    d->strands = (std::max)(1, j.value("strands", d->strands));
    d->radius = j.value("radius", d->radius);
    d->height = j.value("height", d->height);
    d->turns = j.value("turns", d->turns);
    d->spin = j.value("spin", d->spin);
    d->width = j.value("width", d->width);
    d->backWidth = j.value("backWidth", d->backWidth);
    d->backBright = j.value("backBright", d->backBright);
    d->outline = j.value("outline", d->outline);
    d->coreWidth = j.value("coreWidth", d->coreWidth);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->growTime = j.value("growTime", d->growTime);
    d->fadeFrom = j.value("fadeFrom", d->fadeFrom);
    d->segments = (std::max)(2, j.value("segments", d->segments));
    col("outlineColor", d->outlineColor);
    col("color", d->color);
    col("coreColor", d->coreColor);
    return d;
}

std::unique_ptr<EffectPart> HelixDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<Helix>(*this, at);
}

// ---------- 出す ----------
Helix::Helix(const HelixDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_coreTex = def.coreTexture.empty() ? nullptr : TextureManager::Get(def.coreTexture);
    m_pos = XMFLOAT3(at.x, at.y, at.z);
    if (at.hasCam) m_fwd = at.camFwd;
}

// ---------- 描く ----------
void Helix::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Overlay || m_elapsed < 0.0f) return;
    float t = (std::min)(m_elapsed / m_def.life, 1.0f);
    float grow = (m_def.growTime > 0.0f) ? (std::min)(1.0f, m_elapsed / m_def.growTime) : 1.0f;
    float fade = (t > m_def.fadeFrom) ? 1.0f - (t - m_def.fadeFrom) / (std::max)(1.0f - m_def.fadeFrom, 0.01f) : 1.0f;
    float thin = 0.5f + 0.5f * fade;                       // 消える時は細くなる
    float top = m_def.height * grow;                       // 今の高さ（下から伸びる）
    float rot = XMConvertToRadians(m_def.spin) * m_elapsed;
    DrawStrands(r, false, top, thin, fade, rot);          // 奥の線
    if (m_coreTex)                                         // 真ん中の柱（下端が地面）
    {
        XMFLOAT4 c = m_def.coreColor; c.w *= fade;
        r->SetAdditive(true);
        r->DrawBillboard(m_coreTex, m_pos.x, m_pos.y, m_pos.z, m_def.coreWidth * thin, top, 0.0f, c);
        r->SetAdditive(false);
    }
    DrawStrands(r, true, top, thin, fade, rot);           // 手前の線
}

void Helix::DrawStrands(Renderer3D* r, bool front, float top, float thin, float fade, float rot)
{
    float fx = m_fwd.x, fz = m_fwd.z, fl = sqrtf(fx * fx + fz * fz);
    if (fl > 0.0001f) { fx /= fl; fz /= fl; }
    auto pt = [&](int s, float v) {                         // s 本目の、高さの割合 v の点
        float a = rot + v * m_def.turns * XM_2PI + s * XM_2PI / m_def.strands;
        return XMFLOAT3(m_pos.x + cosf(a) * m_def.radius * thin, m_pos.y + v * top, m_pos.z + sinf(a) * m_def.radius * thin);
    };
    for (int pass = 0; pass < 2; pass++)                    // 縁取り（ふつうの重ね）→ 光る線（加算）
    {
        if (pass == 0 && m_def.outline <= 0.0f) continue;
        r->SetAdditive(pass == 1);
        for (int s = 0; s < m_def.strands; s++)
            for (int i = 0; i < m_def.segments; i++)
            {
                float v0 = (float)i / m_def.segments, v1 = (float)(i + 1) / m_def.segments;
                float am = rot + (v0 + v1) * 0.5f * m_def.turns * XM_2PI + s * XM_2PI / m_def.strands;
                float f = 0.5f - 0.5f * (cosf(am) * fx + sinf(am) * fz);   // 1＝手前（カメラの方）、0＝奥
                if ((f >= 0.5f) != front) continue;
                float w = m_def.width * (m_def.backWidth + (1.0f - m_def.backWidth) * f) * thin;
                XMFLOAT3 p0 = pt(s, v0), p1 = pt(s, v1);
                if (pass == 0)
                {
                    XMFLOAT4 oc = m_def.outlineColor; oc.w *= fade;
                    r->DrawBand(m_tex, p0, p1, w + m_def.outline, w + m_def.outline, m_fwd, oc);
                }
                else
                {
                    float br = m_def.backBright + (1.0f - m_def.backBright) * f;
                    XMFLOAT4 c(m_def.color.x * br, m_def.color.y * br, m_def.color.z * br, m_def.color.w * fade);
                    r->DrawBand(m_tex, p0, p1, w, w, m_fwd, c);
                }
            }
    }
    r->SetAdditive(false);
}
