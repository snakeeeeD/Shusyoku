#include "SpriteSheet.h"
#include "Renderer3D.h"
#include "TextureManager.h"
#include <cmath>

// ---------- データ：effects.json の "sheets" の1つを読む ----------
std::unique_ptr<EffectPartDef> SheetDef::Load(const nlohmann::json& s)
{
    auto d = std::make_unique<SheetDef>();
    auto vec3 = [&](const char* key, XMFLOAT3& out)
    {
        if (!s.contains(key)) return false;
        auto& v = s[key];
        out = XMFLOAT3(v[0], v[1], v[2]);
        return true;
    };
    d->texture = s.value("texture", "");
    d->cols = s.value("cols", 1);
    d->rows = s.value("rows", 1);
    d->frames = s.value("frames", 0);
    d->fps = s.value("fps", 20.0f);
    d->scale = s.value("scale", 1.0f);
    d->yOffset = s.value("yOffset", 0.0f);
    d->loop = s.value("loop", false);
    d->startFrame = s.value("startFrame", 0);
    d->delay = s.value("delay", 0.0f);
    d->autoFlipX = s.value("autoFlipX", false);
    d->flipX = s.value("flipX", false);
    d->rotDeg = s.value("rotDeg", 0.0f);
    d->travel = s.value("travel", 0.0f);
    d->useCamera = vec3("fromCamera", d->fromCamera);
    vec3("offset", d->offset);
    vec3("vel", d->vel);
    if (s.contains("color"))
    {
        auto& c = s["color"];
        d->color = XMFLOAT4(c[0], c[1], c[2], c[3]);
    }
    return d;
}

std::unique_ptr<EffectPart> SheetDef::Spawn(const SpawnAt& at) const
{
    return std::make_unique<SpriteSheet>(*this, at);
}

// ---------- 出す ----------
SpriteSheet::SpriteSheet(const SheetDef& s, const SpawnAt& at) : m_def(s)
{
    bool mir = s.autoFlipX && (at.x > 0.0f);          // 盤面右半分なら反転
    float ox = mir ? -s.offset.x : s.offset.x;
    m_pos = XMFLOAT3(at.x + ox, at.y + s.yOffset + s.offset.y, at.z + s.offset.z);   // 既定(ワールド指定)
    m_vel = s.vel; if (mir) m_vel.x = -m_vel.x;

    if (s.useCamera && at.hasCam && s.travel > 0.0f)  // カメラ基準で発生
    {
        const XMFLOAT3& rt = at.camRight;   // カメラ右
        const XMFLOAT3& up = at.camUp;      // カメラ上
        const XMFLOAT3& fw = at.camFwd;     // カメラ前
        const XMFLOAT3& eye = at.camPos;    // カメラ位置
        float fcx = mir ? -s.fromCamera.x : s.fromCamera.x;
        m_pos = XMFLOAT3(
            eye.x + rt.x * fcx + up.x * s.fromCamera.y + fw.x * s.fromCamera.z,
            eye.y + rt.y * fcx + up.y * s.fromCamera.y + fw.y * s.fromCamera.z,
            eye.z + rt.z * fcx + up.z * s.fromCamera.y + fw.z * s.fromCamera.z);
        m_vel = XMFLOAT3((at.x - m_pos.x) / s.travel,                 // 着弾点へ向かう速度を自動計算
            (at.y + s.yOffset - m_pos.y) / s.travel,
            (at.z - m_pos.z) / s.travel);
    }

    m_tex = s.texture.empty() ? nullptr : TextureManager::Get(s.texture);
    m_frames = (s.frames > 0) ? s.frames : s.cols * s.rows;
    m_color = XMFLOAT4(s.color.x * at.tint.x, s.color.y * at.tint.y, s.color.z * at.tint.z, s.color.w * at.tint.w);
    m_elapsed = -s.delay;
    m_rot = at.rot + XMConvertToRadians(s.rotDeg);
    m_flipX = (s.flipX != mir);
}

// ---------- 動かす ----------
void SpriteSheet::Update(float dt)
{
    m_elapsed += dt;
    if (m_elapsed > 0.0f)                 // 遅延中は動かさない
    {
        m_pos.x += m_vel.x * dt;
        m_pos.y += m_vel.y * dt;
        m_pos.z += m_vel.z * dt;
    }
    if (m_fading) m_fade -= dt;
}

bool SpriteSheet::IsDone() const
{
    bool oneShotDone = !m_def.loop && !m_fading && (int)(m_elapsed * m_def.fps) >= m_frames;
    bool fadeDone = m_fading && m_fade <= 0.0f;
    return oneShotDone || fadeDone;
}

void SpriteSheet::Stop(float fadeSec)
{
    if (m_fading) return;
    m_fading = true;
    m_fade = (fadeSec > 0.0f) ? fadeSec : 0.0f;
    m_fadeDur = (fadeSec > 0.0f) ? fadeSec : 1.0f;
}

// ---------- 描く ----------
void SpriteSheet::Draw(Renderer3D* r, EffectLayer layer)
{
    if (layer != EffectLayer::Overlay) return;   // コマ送りは上の順番（キャラの後）でだけ描く
    if (m_elapsed < 0.0f) return;   // 遅延中はまだ出さない
    int f = (int)(m_elapsed * m_def.fps);
    if (m_def.loop && m_frames > 0) f %= m_frames;
    if (f >= m_frames) f = m_frames - 1;
    int cols = m_def.cols, rows = m_def.rows;
    int col = (cols > 0) ? f % cols : 0;
    int row = (cols > 0) ? f / cols : 0;
    XMFLOAT4 uv = m_flipX
        ? XMFLOAT4((float)col / cols, (float)row / rows, 1.0f / cols, 1.0f / rows)
        : XMFLOAT4((float)(col + 1) / cols, (float)row / rows, -1.0f / cols, 1.0f / rows);
    float a = m_fading ? (m_fade / m_fadeDur) : 1.0f;
    if (a < 0.0f) a = 0.0f;
    XMFLOAT4 c = m_color; c.w *= a;

    // 中心アンカー：基準点にスプライト中心が来るよう、上方向ベクトルの半分だけ戻す
    float size = m_def.scale;
    float pitch = XMConvertToRadians(Renderer3D::GetBillboardPitch());
    float sp = sinf(pitch), cp = cosf(pitch);
    float upx = size * sp * sinf(m_rot);
    float upy = size * cp;
    float upz = size * sp * cosf(m_rot);

    r->DrawBillboard(m_tex,
        m_pos.x - 0.5f * upx,
        m_pos.y - 0.5f * upy,
        m_pos.z - 0.5f * upz,
        size, size, m_rot, c, uv);
}
