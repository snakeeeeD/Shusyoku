#pragma once
#include<d3d11.h>
#include<DirectXMath.h>
#include<wrl/client.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex3D
{
	XMFLOAT3 Pos;
	XMFLOAT2 Tex;
};

struct ConstantBuffer3D

{
	XMMATRIX World;
	XMMATRIX View;
	XMMATRIX Projection;
	XMFLOAT4 Color;
};

class Renderer3D
{
public:
	Renderer3D();
	~Renderer3D();

	bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
		int screenWidth, int screenHeight);

    void Begin();
    void End();

    // 床タイル描画
    void DrawTile(ID3D11ShaderResourceView* texture,
        float x, float z, float size,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1), float y = 0.0f);

    void DrawTileEx(ID3D11ShaderResourceView* texture,
        float x, float z, float width, float depth,
        float rotationY,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1));

    // カメラ設定
    void SetCamera(XMFLOAT3 pos, XMFLOAT3 target, XMFLOAT3 up);

    XMMATRIX GetViewMatrix() const { return m_viewMatrix; }
    XMMATRIX GetProjectionMatrix() const { return m_projectionMatrix; }

    void DrawBillboard(ID3D11ShaderResourceView* texture,
        float x, float y, float z,
        float width, float height,
        float rotation,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1));

    // HPバー用
    static constexpr float BILLBOARD_PITCH = -60.0f;        // ビルボードの傾き角度
    static float GetBillboardPitch() { return BILLBOARD_PITCH; }

    void SetDepthWrite(bool enabled);
    void SetDepthEnabled(bool enabled);

private:
    bool CreateShaders();
    bool CreateBuffers();

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
    ComPtr<ID3D11RasterizerState> m_rasterState;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilState> m_depthNoWriteState;
    ComPtr<ID3D11DepthStencilState> m_depthDisabledState;

    ComPtr<ID3D11Buffer> m_billboardVertexBuffer;

    XMMATRIX m_viewMatrix;
    XMMATRIX m_projectionMatrix;

    // カメラ関係
    XMFLOAT3 m_cameraPos;
    float m_cameraPitchAngle; // カメラの仰角

    // スクリーン関係
    int m_screenWidth;
    int m_screenHeight;

};

