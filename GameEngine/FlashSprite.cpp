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
    d->delay = j.value("delay", d->delay);
    d->roll = j.value("roll", d->roll);
    d->openTime = j.value("openTime", d->openTime);
    d->holdTime = j.value("holdTime", d->holdTime);
    d->closeTime = j.value("closeTime", d->closeTime);
    d->glitch = j.value("glitch", d->glitch);
    d->offsetX = j.value("offsetX", d->offsetX);
    d->offsetY = j.value("offsetY", d->offsetY);
    d->spin = j.value("spin", d->spin);
    if (d->openTime > 0.0f) d->life = (std::max)(0.01f, d->openTime + d->holdTime + d->closeTime);   // 開閉の動きは、その合計が寿命
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
    m_elapsed = -def.delay;
    if (at.hasCam) { m_right = at.camRight; m_up = at.camUp; }
    // 画面の右・上へずらす（回す前の、カメラの右と上で）
    float ox = at.mirror ? -def.offsetX : def.offsetX;   // 左右を逆にする時は左へ
    m_pos.x += m_right.x * ox + m_up.x * def.offsetY;
    m_pos.y += m_right.y * ox + m_up.y * def.offsetY;
    m_pos.z += m_right.z * ox + m_up.z * def.offsetY;

    if (def.faceRot)
    {
        // rot の向き（水平）を画面に写したときの角度だけ、絵を画面の中で回す
        float fx = sinf(at.rot), fz = cosf(at.rot);
        float sx = fx * m_right.x + fz * m_right.z;   // 画面の右方向の成分
        float sy = fx * m_up.x + fz * m_up.z;         // 画面の上方向の成分
        if (sx * sx + sy * sy > 0.000001f) Roll(atan2f(sy, sx));
        // 向きがカメラの奥側なら、キャラの後ろ（地面の順番）に描く
        if (at.hasCam && fx * at.camFwd.x + fz * at.camFwd.z > 0.0f) m_layer = EffectLayer::Ground;
    }
    // さらに画面の中で回す：roll（度）と、faceRot でない時は Play の rot（当たるたびに少し傾けるなど）
    Roll(XMConvertToRadians(def.roll) + (def.faceRot ? 0.0f : at.rot));
}

// 絵の右と上を、画面の中で angle（ラジアン）だけ回す
void FlashSprite::Roll(float angle)
{
    if (angle == 0.0f) return;
    float c = cosf(angle), s = sinf(angle);
    XMFLOAT3 r = m_right, u = m_up;
    m_right = XMFLOAT3(r.x * c + u.x * s, r.y * c + u.y * s, r.z * c + u.z * s);
    m_up = XMFLOAT3(-r.x * s + u.x * c, -r.y * s + u.y * c, -r.z * s + u.z * c);
}

// ---------- 描く ----------
// 幅だけ開閉する時の開き具合：開く（イーズアウト）→ 止まる → 閉じる
float FlashSprite::OpenAmount() const
{
    float t = m_elapsed;
    if (t < m_def.openTime) { float u = t / m_def.openTime; return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u); }
    t -= m_def.openTime;
    if (t < m_def.holdTime) return 1.0f;
    t -= m_def.holdTime;
    if (m_def.closeTime <= 0.0f) return 0.0f;
    float u = (std::min)(t / m_def.closeTime, 1.0f);
    return 1.0f - u * u;
}

void FlashSprite::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != m_layer) return;
    if (m_elapsed < 0.0f) return;   // 遅らせている間は出さない
    float t = (std::min)(m_elapsed / m_def.life, 1.0f);   // 寿命の進み具合（0→1）
    float w, h;
    XMFLOAT4 c = m_color;
    if (m_def.openTime > 0.0f)   // 幅だけ開いて閉じる（長さはそのまま）
    {
        w = m_def.sizeStart * m_scale * OpenAmount();
        h = m_def.sizeStart * m_def.aspect * m_scale;
    }
    else                         // 少しふくらみながら、途中から薄くなる
    {
        float grow = 1.0f - powf(1.0f - t, m_def.ease);
        w = (m_def.sizeStart + (m_def.sizeEnd - m_def.sizeStart) * grow) * m_scale;
        h = w * m_def.aspect;
        if (t > m_def.fadeFrom) c.w *= 1.0f - (t - m_def.fadeFrom) / (std::max)(1.0f - m_def.fadeFrom, 0.01f);
    }
    if (w <= 0.0f) return;
    if (m_def.additive) r->SetAdditive(true);
    r->SetDepthEnabled(false);   // 体に食い込んでも隠れないように（前後は描く順番で決める）
    if (m_def.glitch > 0.0f)     // グリッチ：水色と赤紫のコピーを左右にずらして重ね、1フレームごとに逆へぶらす
    {
        float j = m_def.glitch * m_scale * (((int)(m_elapsed * 60.0f) % 2) ? 1.0f : -0.7f);
        DrawQuadAt(r, -j, w, h, XMFLOAT4(c.x * 0.2f, c.y, c.z, c.w * 0.45f));
        DrawQuadAt(r, j, w, h, XMFLOAT4(c.x, c.y * 0.2f, c.z * 0.9f, c.w * 0.45f));
    }
    DrawQuadAt(r, 0.0f, w, h, c);
    r->SetDepthEnabled(true);
    if (m_def.additive) r->SetAdditive(false);
}

void FlashSprite::DrawQuadAt(Renderer3D* r, float ox, float w, float h, const XMFLOAT4& c)
{
    auto at = [&](float ax, float ay) {   // 絵の中の位置（-0.5～0.5）→ ワールド（画面の右へ ox ずらす）
        float rx = ax * w + ox, uy = ay * h;
        return XMFLOAT3(m_pos.x + m_right.x * rx + m_up.x * uy,
                        m_pos.y + m_right.y * rx + m_up.y * uy,
                        m_pos.z + m_right.z * rx + m_up.z * uy);
    };
    r->DrawQuad(m_tex, at(-0.5f, 0.5f), at(0.5f, 0.5f), at(-0.5f, -0.5f), at(0.5f, -0.5f), XMFLOAT4(0, 0, 1, 1), c);
}
