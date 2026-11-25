#include "ScreenOverlay.h"
#include "EffectDataBase.h"   // FxMaterial・GetMaterial
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cstdlib>
#include <Windows.h>

// ---------- データ：effects.json の "parts" の1つ（"type": "screen"）を読む ----------
std::unique_ptr<EffectPartDef> ScreenOverlayDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<ScreenOverlayDef>();
    d->material = j.value("material", d->material);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->fadeIn = j.value("fadeIn", d->fadeIn);
    if (j.contains("color"))
    {
        auto& c = j["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> ScreenOverlayDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<ScreenOverlay>(*this, at);
}

// ---------- 出す ----------
ScreenOverlay::ScreenOverlay(const ScreenOverlayDef& def, const SpawnAt& at) : m_def(def)
{
    m_mat = EffectDataBase::GetMaterial(def.material);
    if (!m_mat)
    {
        OutputDebugStringA(("[effects.json] screen: unknown material: " + def.material + "\n").c_str());
        return;
    }
    m_shape = TextureManager::Get(m_mat->shape);
    m_noise = TextureManager::Get(m_mat->noise);
    m_ramp = TextureManager::Get(m_mat->ramp);
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_seed = (float)rand() / RAND_MAX * 10.0f;
}

// ---------- 描く ----------
void ScreenOverlay::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Screen || !m_mat) return;
    FxParams fx;
    fx.life = (std::min)(m_elapsed / m_def.life, 1.0f);   // 寿命の進み具合（0→1）
    fx.erodeStart = m_mat->erodeStart; fx.erodeEnd = m_mat->erodeEnd;
    fx.glow = m_mat->glow; fx.scroll = m_mat->scroll; fx.noiseScale = m_mat->noiseScale;
    fx.time = m_elapsed; fx.seed = m_seed;
    fx.appear = m_mat->appear;
    XMFLOAT4 c = m_color;
    if (m_def.fadeIn > 0.0f) c.w *= (std::min)(1.0f, m_elapsed / m_def.fadeIn);
    if (m_mat->additive) r->SetAdditive(true);
    r->DrawFxScreen(m_shape, m_noise, m_ramp, fx, c);
    if (m_mat->additive) r->SetAdditive(false);
}
