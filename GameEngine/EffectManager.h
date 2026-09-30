#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include "Renderer3D.h"
#include "EffectDataBase.h"
#include "TextureManager.h"
#include "ScreenShake.h"

using namespace DirectX;

struct Particle
{
    XMFLOAT3 pos, vel;
    XMFLOAT4 colorStart, colorEnd;
    float life, lifeMax;
    float scale, gravity, drag;
    ID3D11ShaderResourceView* tex = nullptr;
};

struct SpriteInstance
{
    XMFLOAT3 pos;
    ID3D11ShaderResourceView* tex = nullptr;
    int cols, rows, frames;
    float fps, size, elapsed = 0.0f;
    XMFLOAT4 color;
    bool loop = false;
    int handle = 0;
    bool fading = false;
    float fade = 0.0f, fadeDur = 0.3f; 
    float rot = 0.0f;
    XMFLOAT3 vel = { 0, 0, 0 };
    int startFrame = 0;
    bool flipX = false;               // 既定の向きを左右反転する
};

// パーティクルの発生・更新・描画を1箇所に集約
class EffectManager
{
public:
    static void Update(float dt)
    {
        for (size_t i = 0; i < m_pending.size(); )
        {
            m_pending[i].delay -= dt;
            if (m_pending[i].delay <= 0.0f)
            {
                PendingPlay p = m_pending[i];
                m_pending[i] = m_pending.back(); m_pending.pop_back();
                Play(p.id, p.x, p.y, p.z, p.rot, 0.0f, p.tint);   // 遅延到来→即再生
            }
            else ++i;
        }
        for (auto& p : m_particles)
        {
            p.life -= dt;
            p.vel.y -= p.gravity * dt;
            float d = 1.0f - p.drag;
            p.vel.x *= d; p.vel.z *= d;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
            p.pos.z += p.vel.z * dt;
        }
        for (size_t i = 0; i < m_particles.size(); )   // 寿命切れをswap-and-pop
        {
            if (m_particles[i].life <= 0.0f)
            {
                m_particles[i] = m_particles.back();
                m_particles.pop_back();
            }
            else ++i;
        }
        for (auto& s : m_sprites)
        {
            s.elapsed += dt;
            if (s.elapsed > 0.0f)                 // 遅延中は動かさない
            {
                s.pos.x += s.vel.x * dt;
                s.pos.y += s.vel.y * dt;
                s.pos.z += s.vel.z * dt;
            }
            if (s.fading) s.fade -= dt;
        }
        for (size_t i = 0; i < m_sprites.size(); )
        {
            auto& s = m_sprites[i];
            bool oneShotDone = !s.loop && !s.fading && (int)(s.elapsed * s.fps) >= s.frames;
            bool fadeDone = s.fading && s.fade <= 0.0f;
            if (oneShotDone || fadeDone) { m_sprites[i] = m_sprites.back(); m_sprites.pop_back(); }
            else ++i;
        }
    }

    static void Draw(Renderer3D* r, ID3D11ShaderResourceView* tex)
    {
        s_invView = XMMatrixInverse(nullptr, r->GetViewMatrix());   // 逆ビュー＝カメラの姿勢
        s_hasCam = true;

        const float SCALE = 2.0f;   // 全体のエフェクト倍率（ここ1つで調整）
        for (auto& p : m_particles)
        {
            float t = 1.0f - p.life / p.lifeMax;
            XMFLOAT4 c(
                p.colorStart.x + (p.colorEnd.x - p.colorStart.x) * t,
                p.colorStart.y + (p.colorEnd.y - p.colorStart.y) * t,
                p.colorStart.z + (p.colorEnd.z - p.colorStart.z) * t,
                p.colorStart.w + (p.colorEnd.w - p.colorStart.w) * t);
            ID3D11ShaderResourceView* pt = p.tex ? p.tex : tex;
            float sc = p.scale * SCALE;                          // ← 倍率をかける
            r->DrawBillboard(pt, p.pos.x, p.pos.y, p.pos.z, sc, sc, 0.0f, c);
        }

        for (auto& s : m_sprites)
        {
            if (s.elapsed < 0.0f) continue;   // 遅延中はまだ出さない
            int f = (int)(s.elapsed * s.fps);

            if (s.loop && s.frames > 0) f %= s.frames;
            if (f >= s.frames) f = s.frames - 1;
            int col = (s.cols > 0) ? f % s.cols : 0;
            int row = (s.cols > 0) ? f / s.cols : 0;
            XMFLOAT4 uv = s.flipX
                ? XMFLOAT4((float)(col + 1) / s.cols, (float)row / s.rows, -1.0f / s.cols, 1.0f / s.rows)
                : XMFLOAT4((float)col / s.cols, (float)row / s.rows, 1.0f / s.cols, 1.0f / s.rows);
            float a = s.fading ? (s.fade / s.fadeDur) : 1.0f;
            if (a < 0.0f) a = 0.0f;
            XMFLOAT4 c = s.color; c.w *= a;

            // 中心アンカー：基準点にスプライト中心が来るよう、上方向ベクトルの半分だけ戻す
            float pitch = XMConvertToRadians(Renderer3D::GetBillboardPitch());
            float sp = sinf(pitch), cp = cosf(pitch);
            float upx = s.size * sp * sinf(s.rot);
            float upy = s.size * cp;
            float upz = s.size * sp * cosf(s.rot);

            r->DrawBillboard(s.tex,
                s.pos.x - 0.5f * upx,
                s.pos.y - 0.5f * upy,
                s.pos.z - 0.5f * upz,
                s.size, s.size, s.rot, c, uv);
        }
    }

    static void Clear() { m_particles.clear(); m_sprites.clear(); }

    // 球状にばらまく（講義のCreateRingの分布を流用）
    static void SpawnBurst(float x, float y, float z, int count,
        float speed, XMFLOAT4 colorStart, XMFLOAT4 colorEnd, float life, float scale,
        float gravity = 4.0f, float drag = 0.02f,
        ID3D11ShaderResourceView* tex = nullptr)
    {
        const float golden = XM_PI * (3.0f - sqrtf(5.0f));
        for (int i = 0; i < count; i++)
        {
            float tt = (count > 1) ? (float)i / (count - 1) : 0.5f;
            float phi = acosf(1.0f - 2.0f * tt);
            float theta = golden * i;
            float sp = sinf(phi);
            float s = speed * (0.5f + Rand01() * 0.5f);

            Particle p;
            p.pos = XMFLOAT3(x, y, z);
            p.vel = XMFLOAT3(sp * cosf(theta) * s,
                cosf(phi) * s * 0.6f + speed * 0.5f,   // やや上向き
                sp * sinf(theta) * s);
            p.colorStart = colorStart;
            p.colorEnd = colorEnd;
            p.lifeMax = life * (0.7f + Rand01() * 0.6f);
            p.life = p.lifeMax;
            p.scale = scale;
            p.gravity = gravity;
            p.drag = drag;
            p.tex = tex;
            m_particles.push_back(p);
        }
    }

    // 名前でエフェクトを再生（effects.jsonのプリセット）
    static int Play(const std::string& id, float x, float y, float z, float rot = 0.0f, float delay = 0.0f,
        XMFLOAT4 tint = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f))
    {
        if (delay > 0.0f)
        {
            m_pending.push_back({ id, x, y, z, rot, tint, delay });
            return s_nextHandle++;
        }
        int handle = s_nextHandle++;
        const EffectDef* def = EffectDataBase::Get(id);
        if (!def) return handle;
        if (def->shake > 0.0f) ScreenShake::Add(def->shake);
        for (auto& b : def->bursts)
        {
            ID3D11ShaderResourceView* t = b.texture.empty() ? nullptr : TextureManager::Get(b.texture);
            SpawnBurst(x, y, z, b.count, b.speed,
                b.colorStart, b.colorEnd, b.life, b.scale, b.gravity, b.drag, t);
        }
        for (auto& s : def->sheets)
        {
            SpriteInstance si;
            bool mir = s.autoFlipX && (x > 0.0f);          // 盤面右半分なら反転
            float ox = mir ? -s.offset.x : s.offset.x;

            XMFLOAT3 sp(x + ox, y + s.yOffset + s.offset.y, z + s.offset.z);   // 既定(ワールド指定)
            XMFLOAT3 sv = s.vel; if (mir) sv.x = -sv.x;

            if (s.useCamera && s_hasCam && s.travel > 0.0f)                    // カメラ基準で発生
            {
                XMFLOAT3 rt, up, fw, eye;
                XMStoreFloat3(&rt, s_invView.r[0]);   // カメラ右
                XMStoreFloat3(&up, s_invView.r[1]);   // カメラ上
                XMStoreFloat3(&fw, s_invView.r[2]);   // カメラ前
                XMStoreFloat3(&eye, s_invView.r[3]);   // カメラ位置
                float fcx = mir ? -s.fromCamera.x : s.fromCamera.x;
                sp = XMFLOAT3(
                    eye.x + rt.x * fcx + up.x * s.fromCamera.y + fw.x * s.fromCamera.z,
                    eye.y + rt.y * fcx + up.y * s.fromCamera.y + fw.y * s.fromCamera.z,
                    eye.z + rt.z * fcx + up.z * s.fromCamera.y + fw.z * s.fromCamera.z);
                sv = XMFLOAT3((x - sp.x) / s.travel,                 // 着弾点へ向かう速度を自動計算
                    (y + s.yOffset - sp.y) / s.travel,
                    (z - sp.z) / s.travel);
            }

            si.pos = sp;
            si.tex = s.texture.empty() ? nullptr : TextureManager::Get(s.texture);
            si.cols = s.cols; si.rows = s.rows;
            si.frames = (s.frames > 0) ? s.frames : s.cols * s.rows;
            si.fps = s.fps; si.size = s.scale;
            si.color = s.color;
            si.color.x *= tint.x; si.color.y *= tint.y; si.color.z *= tint.z; si.color.w *= tint.w;
            si.loop = s.loop;
            si.elapsed = -(delay + s.delay);
            si.handle = handle;
            si.rot = rot + XMConvertToRadians(s.rotDeg);
            si.vel = sv;
            si.flipX = mir;
            si.startFrame = s.startFrame;
            m_sprites.push_back(si);
        }
        if (!def->then.empty())
            Play(def->then, x, y, z, rot, delay + def->thenDelay, tint);   // 着弾後に連鎖
        return handle;
    }

    // 指定ハンドルをフェードアウトさせて消す（fadeSec<=0で即消し）
    static void Stop(int handle, float fadeSec = 0.3f)
    {
        if (handle == 0) return;
        for (auto& s : m_sprites)
        {
            if (s.handle == handle && !s.fading)
            {
                s.fading = true;
                s.fade = (fadeSec > 0.0f) ? fadeSec : 0.0f;
                s.fadeDur = (fadeSec > 0.0f) ? fadeSec : 1.0f;
            }
        }
    }

private:
    static inline std::vector<Particle> m_particles;
    static inline std::vector<SpriteInstance> m_sprites;
    struct PendingPlay { std::string id; float x, y, z, rot; XMFLOAT4 tint; float delay; };
    static inline std::vector<PendingPlay> m_pending;
    static inline int s_nextHandle = 1;
    static float Rand01() { return (float)rand() / RAND_MAX; }
    static inline XMMATRIX s_invView = XMMatrixIdentity();
    static inline bool s_hasCam = false;
};