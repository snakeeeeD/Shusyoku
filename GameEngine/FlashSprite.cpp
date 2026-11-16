#include "FlashSprite.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <algorithm>
#include <cmath>

// ---------- データ：effects.json の "parts" の1つ（"type": "flash"）を読む ----------
std::unique_ptr<EffectPartDef> FlashSpriteDef::Load(const nlohmann::json& j)
{
    auto d = std::make_unique<FlashSpriteDef>();
    d->texture = j.value("texture", d->texture);
    d->life = (std::max)(0.01f, j.value("life", d->life));   // 0秒だと割り算できないので下限
    d->sizeStart = j.value("sizeStart", d->sizeStart);
    d->sizeEnd = j.value("sizeEnd", d->sizeEnd);
    d->aspect = j.value("aspect", d->aspect);
    d->ease = j.value("ease", d->ease);
    d->fadeFrom = j.value("fadeFrom", d->fadeFrom);
    d->additive = j.value("additive", d->additive);
    d->faceRot = j.value("faceRot", d->faceRot);
    if (j.contains("color"))
    {
        auto& c = j["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> FlashSpriteDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<FlashSprite>(*this, at);
}

// ---------- 出す ----------
FlashSprite::FlashSprite(const FlashSpriteDef& def, const SpawnAt& at) : m_def(def)
{
    m_tex = TextureManager::Get(def.texture);
    m_pos = XMFLOAT3(at.x, at.y, at.z);
    m_color = XMFLOAT4(def.color.x * at.tint.x, def.color.y * at.tint.y, def.color.z * at.tint.z, def.color.w * at.tint.w);
    m_scale = at.scale;
    if (at.hasCam) { m_right = at.camRight; m_up = at.camUp; }
    if (!def.faceRot) return;

    // rot の向き（水平）を画面に写したときの角度だけ、絵を画面の中で回す
    float fx = sinf(at.rot), fz = cosf(at.rot);
    float sx = fx * m_right.x + fz * m_right.z;   // 画面の右方向の成分
    float sy = fx * m_up.x + fz * m_up.z;         // 画面の上方向の成分
    float len = sqrtf(sx * sx + sy * sy);
    if (len > 0.001f)
    {
        float c = sx / len, s = sy / len;
        XMFLOAT3 r = m_right, u = m_up;
        m_right = XMFLOAT3(r.x * c + u.x * s, r.y * c + u.y * s, r.z * c + u.z * s);
        m_up = XMFLOAT3(-r.x * s + u.x * c, -r.y * s + u.y * c, -r.z * s + u.z * c);
    }
    // 向きがカメラの奥側なら、キャラの後ろ（地面の順番）に描く
    if (at.hasCam && fx * at.camFwd.x + fz * at.camFwd.z > 0.0f) m_layer = EffectLayer::Ground;
}

// ---------- 描く ----------
void FlashSprite::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != m_layer) return;
    float t = (std::min)(m_elapsed / m_def.life, 1.0f);   // 寿命の進み具合（0→1）
    float grow = 1.0f - powf(1.0f - t, m_def.ease);
    float w = (m_def.sizeStart + (m_def.sizeEnd - m_def.sizeStart) * grow) * m_scale;
    float h = w * m_def.aspect;
    XMFLOAT4 c = m_color;
    if (t > m_def.fadeFrom) c.w *= 1.0f - (t - m_def.fadeFrom) / (std::max)(1.0f - m_def.fadeFrom, 0.01f);
    auto at = [&](float ax, float ay) {   // 絵の中の位置（-0.5～0.5）→ ワールド
        return XMFLOAT3(m_pos.x + m_right.x * ax * w + m_up.x * ay * h,
                        m_pos.y + m_right.y * ax * w + m_up.y * ay * h,
                        m_pos.z + m_right.z * ax * w + m_up.z * ay * h);
    };
    if (m_def.additive) r->SetAdditive(true);
    r->SetDepthEnabled(false);   // 体に食い込んでも隠れないように（前後は描く順番で決める）
    r->DrawQuad(m_tex, at(-0.5f, 0.5f), at(0.5f, 0.5f), at(-0.5f, -0.5f), at(0.5f, -0.5f), XMFLOAT4(0, 0, 1, 1), c);
    r->SetDepthEnabled(true);
    if (m_def.additive) r->SetAdditive(false);
}
