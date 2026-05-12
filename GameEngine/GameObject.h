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
};