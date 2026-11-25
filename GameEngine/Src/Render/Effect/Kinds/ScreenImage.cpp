#include "ScreenImage.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "screenImage"）を読む ----------
std::unique_ptr<EffectPartDef> ScreenImageDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<ScreenImageDef>();
    d->texture = j.value("texture", d->texture);
    if (j.contains("rect")) { auto& v = j["rect"]; d->rect = XMFLOAT4(v[0], v[1], v[2], v[3]); }
    if (j.contains("from")) { auto& v = j["from"]; d->from = XMFLOAT2(v[0], v[1]); }
    d->delay = j.value("delay", d->delay);
    d->moveTime = j.value("moveTime", d->moveTime);
    d->ease = j.value("ease", d->ease);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->fadeIn = j.value("fadeIn", d->fadeIn);
    d->fadeOut = j.value("fadeOut", d->fadeOut);
    d->additive = j.value("additive", d->additive);
    d->overUI = j.value("overUI", d->overUI);
    if (j.contains("color")) { auto& c = j["color"]; d->color = XMFLOAT4(c[0], c[1], c[2], c[3]); }
    return d;
}

std::unique_ptr<EffectPart> ScreenImageDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<ScreenImage>(*this, at);
}

// ---------- 出す ----------
ScreenImage::ScreenImage(const ScreenImageDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_elapsed = -def.delay;
}

// ---------- 描く ----------
void ScreenImage::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != (m_def.overUI ? EffectLayer::Top : EffectLayer::Screen) || !m_tex) return;
    if (m_elapsed < 0.0f) return;   // 待ち（delay）の間はまだ出さない
    float u = (m_def.moveTime > 0.0f) ? (std::min)(m_elapsed / m_def.moveTime, 1.0f) : 1.0f;
    float rest = 1.0f - powf(u, m_def.ease);   // 止まる所までの残り（1→0。ease が大きいほど最後に一気に詰める）
    float dx = m_def.from.x * rest, dy = m_def.from.y * rest;
    XMFLOAT4 c = m_color;
    if (m_def.fadeIn > 0.0f) c.w *= (std::min)(1.0f, m_elapsed / m_def.fadeIn);
    float left = m_def.life - m_elapsed;       // 消えるまでの残り秒数
    if (m_def.fadeOut > 0.0f && left < m_def.fadeOut) c.w *= (std::max)(0.0f, left / m_def.fadeOut);
    if (m_def.additive) r->SetAdditive(true);
    r->DrawScreenImage(m_tex, m_def.rect.x + dx, m_def.rect.y + dy, m_def.rect.z + dx, m_def.rect.w + dy, c);
    if (m_def.additive) r->SetAdditive(false);
}
