#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
using namespace DirectX;

class GameObject
{
public:
    GameObject();
    virtual ~GameObject() {}

    virtual void Update(float deltaTime) {}
    virtual void Draw(class SpriteRenderer* renderer);      // 2D描画
    virtual void Draw3D(class Renderer3D* renderer);        // 3D描画

    // 2D用（UI、タイトル、フィールドなど）
    float x, y;
    float width, height;
    float rotation;

    // 3D用（バトル）
    float worldX, worldY, worldZ;
    int gridCol, gridRow;

    // 共通
    ID3D11ShaderResourceView* texture;
    XMFLOAT4 color;
    bool isActive;

    // 移動アニメーション
    float m_startWorldX = 0, m_startWorldZ = 0;
    float m_targetWorldX = 0, m_targetWorldZ = 0;
    float m_moveTimer = 0;
    float m_moveDuration = 0.25f;
    bool m_isMoving = false;

    void StartMove(float toX, float toZ, float duration = 1.6f)
    {
        m_startWorldX = worldX;
        m_startWorldZ = worldZ;
        m_targetWorldX = toX;
        m_targetWorldZ = toZ;
        m_moveTimer = 0;
        m_moveDuration = duration;
        m_isMoving = true;
    }

    void UpdateMove(float deltaTime)
    {
        if (!m_isMoving) return;
        m_moveTimer += deltaTime;
        float t = m_moveTimer / m_moveDuration;
        if (t >= 1.0f)
        {
            t = 1.0f;
            m_isMoving = false;
        }
        float ease = t * t * (3.0f - 2.0f * t);
        worldX = m_startWorldX + (m_targetWorldX - m_startWorldX) * ease;
        worldZ = m_startWorldZ + (m_targetWorldZ - m_startWorldZ) * ease;
    }

    bool IsMoving() const { return m_isMoving; }
};