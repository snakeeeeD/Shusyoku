#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <cstdlib>
#include "Renderer3D.h"

using namespace DirectX;

struct Particle
{
    XMFLOAT3 pos, vel;
    XMFLOAT4 colorStart, colorEnd;
    float life, lifeMax;
    float scale, gravity, drag;
};

// パーティクルの発生・更新・描画を1箇所に集約
class EffectManager
{
public:
    static void Update(float dt)
    {
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
    }

    static void Draw(Renderer3D* r, ID3D11ShaderResourceView* tex)
    {
        for (auto& p : m_particles)
        {
            float t = 1.0f - p.life / p.lifeMax;    // 0→1
            XMFLOAT4 c(
                p.colorStart.x + (p.colorEnd.x - p.colorStart.x) * t,
                p.colorStart.y + (p.colorEnd.y - p.colorStart.y) * t,
                p.colorStart.z + (p.colorEnd.z - p.colorStart.z) * t,
                p.colorStart.w + (p.colorEnd.w - p.colorStart.w) * t);
            r->DrawBillboard(tex, p.pos.x, p.pos.y, p.pos.z, p.scale, p.scale, 0.0f, c);
        }
    }

    static void Clear() { m_particles.clear(); }

    // 球状にばらまく（講義のCreateRingの分布を流用）
    static void SpawnBurst(float x, float y, float z, int count,
        float speed, XMFLOAT4 colorStart, XMFLOAT4 colorEnd, float life, float scale)
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
            p.gravity = 4.0f;
            p.drag = 0.02f;
            m_particles.push_back(p);
        }
    }

private:
    static inline std::vector<Particle> m_particles;
    static float Rand01() { return (float)rand() / RAND_MAX; }
};