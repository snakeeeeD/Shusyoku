#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>
#include <wrl/client.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// 頂点構造体
struct SpriteVertex
{
    XMFLOAT3 Pos;   // 位置
    XMFLOAT2 Tex;   // テクスチャ座標
};

// スプライト描画クラス
class SpriteRenderer
{
public:
    SpriteRenderer();
    ~SpriteRenderer();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context, int screenWidth, int screenHeight);
    void Shutdown();

    // テクスチャ読み込み
    bool LoadTexture(const wchar_t* filename, ID3D11ShaderResourceView** texture);

    // スプライト描画
    void DrawSprite(ID3D11ShaderResourceView* texture,
        float x, float y,
        float width, float height,
        float rotation = 0.0f,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1));

    void Begin();
    void End();

private:
    bool CreateShaders();
    bool CreateBuffers();
    void UpdateConstantBuffer(const XMMATRIX& world);

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_samplerState;
    ComPtr<ID3D11BlendState> m_blendState;

    XMMATRIX m_projectionMatrix;
    int m_screenWidth;
    int m_screenHeight;

    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
};

// 定数バッファ
struct ConstantBuffer
{
    XMMATRIX World;
    XMMATRIX Projection;
    XMFLOAT4 Color;
};
