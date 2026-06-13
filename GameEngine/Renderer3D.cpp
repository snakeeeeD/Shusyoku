#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include "Renderer3D.h"

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

Renderer3D::Renderer3D()
    : m_device(nullptr), m_context(nullptr)
    , m_screenWidth(0), m_screenHeight(0)
{
}

Renderer3D::~Renderer3D() {}

bool Renderer3D::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    int screenWidth, int screenHeight)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // デフォルトカメラ（斜め上から見下ろす）
    SetCamera(
        XMFLOAT3(-0.6f, 13.0f, 5.0f),   // カメラ位置
        XMFLOAT3(-0.6f, -2.0f, 0.0f),   // 注視点
        XMFLOAT3(0.0f, 1.0f, 0.0f)    // 上方向
    );

    // 透視投影行列
    m_projectionMatrix = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(45.0f),
        (float)screenWidth / (float)screenHeight,
        0.1f, 100.0f
    );

    if (!CreateShaders()) return false;
    if (!CreateBuffers()) return false;

    // サンプラー
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(m_device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf())))
        return false;

    // ブレンドステート
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendState.GetAddressOf())))
        return false;

    // ラスタライザー
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode = D3D11_FILL_SOLID;
    rasterDesc.CullMode = D3D11_CULL_NONE;
    rasterDesc.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rasterDesc, m_rasterState.GetAddressOf())))
        return false;

    // 深度ステンシル
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = screenWidth;
    depthDesc.Height = screenHeight;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ComPtr<ID3D11Texture2D> depthTexture;
    if (FAILED(m_device->CreateTexture2D(&depthDesc, nullptr, depthTexture.GetAddressOf())))
        return false;
    if (FAILED(m_device->CreateDepthStencilView(depthTexture.Get(), nullptr, m_depthStencilView.GetAddressOf())))
        return false;

    return true;
}

void Renderer3D::SetCamera(XMFLOAT3 pos, XMFLOAT3 target, XMFLOAT3 up)
{
    m_cameraPos = pos;
    m_viewMatrix = XMMatrixLookAtLH(
        XMLoadFloat3(&pos),
        XMLoadFloat3(&target),
        XMLoadFloat3(&up)
    );

    // カメラのX軸回転角を自動計算
    float dx = pos.x - target.x;
    float dy = pos.y - target.y;
    float dz = pos.z - target.z;
    float horizontalDist = sqrtf(dx * dx + dz * dz);
    m_cameraPitchAngle = atan2f(dy, horizontalDist); // ラジアン
}

bool Renderer3D::CreateShaders()
{
    HRESULT hr;
    ID3DBlob* pVSBlob = nullptr;
    ID3DBlob* pPSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    hr = D3DCompileFromFile(L"Shaders/3D_VS.hlsl", nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob) {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    hr = m_device->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
        nullptr, m_vertexShader.GetAddressOf());
    if (FAILED(hr)) {
        pVSBlob->Release();
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = m_device->CreateInputLayout(layout, 2, pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    pVSBlob->Release();
    if (FAILED(hr)) return false;

    hr = D3DCompileFromFile(L"Shaders/3D_PS.hlsl", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob) {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    hr = m_device->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(),
        nullptr, m_pixelShader.GetAddressOf());
    pPSBlob->Release();
    if (FAILED(hr)) return false;

    return true;
}

bool Renderer3D::CreateBuffers()
{
    // 床タイル用頂点（XZ平面）
    Vertex3D vertices[] =
    {
        { XMFLOAT3(-0.5f, 0.0f,  0.5f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f, 0.0f,  0.5f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, 0.0f, -0.5f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, 0.0f, -0.5f), XMFLOAT2(1.0f, 1.0f) },
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;
    if (FAILED(m_device->CreateBuffer(&bd, &initData, m_vertexBuffer.GetAddressOf())))
        return false;

    WORD indices[] = { 0, 1, 2, 2, 1, 3 };
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    initData.pSysMem = indices;
    if (FAILED(m_device->CreateBuffer(&bd, &initData, m_indexBuffer.GetAddressOf())))
        return false;

    bd.ByteWidth = sizeof(ConstantBuffer3D);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_constantBuffer.GetAddressOf())))
        return false;


    // ビルボード用頂点（XY平面）
    Vertex3D billboardVertices[] =
    {
        { XMFLOAT3(-0.5f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f,  0.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f,  0.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
    };

    bd.ByteWidth = sizeof(billboardVertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    initData.pSysMem = billboardVertices;
    if (FAILED(m_device->CreateBuffer(&bd, &initData, m_billboardVertexBuffer.GetAddressOf())))
        return false;
    return true;
}

void Renderer3D::Begin()
{
    // 深度バッファクリア
    m_context->ClearDepthStencilView(m_depthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

    // 深度バッファをセット
    ID3D11RenderTargetView* rtv = nullptr;
    m_context->OMGetRenderTargets(1, &rtv, nullptr);
    m_context->OMSetRenderTargets(1, &rtv, m_depthStencilView.Get());
    if (rtv) rtv->Release();

    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    UINT stride = sizeof(Vertex3D);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    float blendFactor[4] = {};
    m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);

    m_context->RSSetState(m_rasterState.Get());
}

void Renderer3D::End()
{
    // 深度バッファを外す
    ID3D11RenderTargetView* rtv = nullptr;
    m_context->OMGetRenderTargets(1, &rtv, nullptr);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    if (rtv) rtv->Release();
}

void Renderer3D::DrawTile(ID3D11ShaderResourceView* texture,
    float x, float z, float size,
    const XMFLOAT4& color)
{
    XMMATRIX world = XMMatrixScaling(size, 1.0f, size) *
        XMMatrixTranslation(x, 0.0f, z);

    ConstantBuffer3D cb;
    cb.World = XMMatrixTranspose(world);
    cb.View = XMMatrixTranspose(m_viewMatrix);
    cb.Projection = XMMatrixTranspose(m_projectionMatrix);
    cb.Color = color;

    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, &texture);
    m_context->DrawIndexed(6, 0, 0);
}

void Renderer3D::DrawBillboard(ID3D11ShaderResourceView* texture,
    float x, float y, float z,
    float width, float height,
    float rotation,
    const XMFLOAT4& color)
{
    XMMATRIX scale = XMMatrixScaling(width, height, 1.0f);
    XMMATRIX rotY = XMMatrixRotationY(rotation);
    XMMATRIX rotX = XMMatrixRotationX(XMConvertToRadians(BILLBOARD_PITCH));  // 角度をカメラのほうに
    XMMATRIX trans = XMMatrixTranslation(x, y, z);
    XMMATRIX world = scale * rotX * rotY * trans;

    ConstantBuffer3D cb;
    cb.World = XMMatrixTranspose(world);
    cb.View = XMMatrixTranspose(m_viewMatrix);
    cb.Projection = XMMatrixTranspose(m_projectionMatrix);
    cb.Color = color;

    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, &texture);

    UINT stride = sizeof(Vertex3D);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_billboardVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->DrawIndexed(6, 0, 0);
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
}