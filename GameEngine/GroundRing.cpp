#include "GroundRing.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "ring"）を読む ----------
std::unique_ptr<EffectPartDef> GroundRingDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<GroundRingDef>();
    d->texture = j.value("texture", d->texture);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->sizeStart = j.value("sizeStart", d->sizeStart);
    d->sizeEnd = j.value("sizeEnd", d->sizeEnd);
    d->ease = j.value("ease", d->ease);
    d->delay = j.value("delay", d->delay);
    d->y = j.value("y", d->y);
    d->additive = j.value("additive", d->additive);
    if (j.contains("color"))
    {
        auto& c = j["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> GroundRingDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<GroundRing>(*this, at);
}

// ---------- 出す ----------
GroundRing::GroundRing(const GroundRingDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_x = at.x; m_z = at.z;   // 高さは使わず、出した所の真下の地面に置く
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_scale = at.scale;
    m_elapsed = -def.delay;
}

// ---------- 描く ----------
void GroundRing::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Ground) return;   // 地面に寝ているので、キャラより先に描く
    if (m_elapsed < 0.0f) return;   // 遅延中はまだ出さない
    float t = (std::min)(m_elapsed / m_def.life, 1.0f);   // 寿命の進み具合（0→1）
    float grow = 1.0f - powf(1.0f - t, m_def.ease);       // 最初に一気に広がって、だんだんゆっくり（イーズアウト）
    float size = (m_def.sizeStart + (m_def.sizeEnd - m_def.sizeStart) * grow) * m_scale;
    XMFLOAT4 c = m_color;
    c.w *= 1.0f - t;                                       // 広がりながら薄くなって消える
    if (m_def.additive) r->SetAdditive(true);
    r->DrawTileEx(m_tex, m_x, m_z, size, size, 0.0f, c, m_def.y);
    if (m_def.additive) r->SetAdditive(false);
}
