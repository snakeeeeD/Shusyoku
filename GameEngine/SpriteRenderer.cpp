#include "SpriteRenderer.h"
#include "TextureLoader.h"
#include <d3dcompiler.h>
#include <wincodec.h>

#pragma comment(lib, "d3dcompiler.lib")



SpriteRenderer::SpriteRenderer()
    : m_device(nullptr)
    , m_context(nullptr)
    , m_screenWidth(0)
    , m_screenHeight(0)
{
}

SpriteRenderer::~SpriteRenderer()
{
    Shutdown();
}

bool SpriteRenderer::Init(ID3D11Device* device, ID3D11DeviceContext* context, int screenWidth, int screenHeight)
{
    m_device = device;
    m_context = context;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // 射影行列作成（2D用の正射影）
    m_projectionMatrix = XMMatrixOrthographicOffCenterLH(
        0.0f, (float)screenWidth,
        (float)screenHeight, 0.0f,
        0.0f, 1.0f
    );

    wchar_t buffer[256];
    swprintf_s(buffer, L"Screen: %d x %d\n", screenWidth, screenHeight);
    OutputDebugString(buffer);

    if (!CreateShaders())
        return false;

    if (!CreateBuffers())
        return false;

    // サンプラーステート作成
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.MaxAnisotropy = 1;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;

    if (FAILED(m_device->CreateSamplerState(&sampDesc, m_samplerState.GetAddressOf())))
        return false;

    // ブレンドステート作成（アルファブレンド）
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

    // 深度テスト無効
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable = FALSE; // 深度テスト無効
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(m_device->CreateDepthStencilState(&depthDesc, m_depthStencilState.GetAddressOf())))
        return false;


    return true;
}

void SpriteRenderer::Shutdown()
{
    // ComPtrが自動的に解放
}

bool SpriteRenderer::CreateShaders()
{
    HRESULT hr;
    ID3DBlob* pVSBlob = nullptr;
    ID3DBlob* pPSBlob = nullptr;
    ID3DBlob* pErrorBlob = nullptr;

    // 頂点シェーダー
    hr = D3DCompileFromFile(L"Shaders/Sprite2D_VS.hlsl", nullptr, nullptr,
        "main", "vs_5_0", 0, 0, &pVSBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
            OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
            pErrorBlob->Release();
        }
        return false;
    }

    // 頂点シェーダー作成
    hr = m_device->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(),
        nullptr, m_vertexShader.GetAddressOf());
    if (FAILED(hr)) { pVSBlob->Release(); return false; }

    // 入力レイアウト
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = m_device->CreateInputLayout(layout, 2, pVSBlob->GetBufferPointer(),
        pVSBlob->GetBufferSize(), m_inputLayout.GetAddressOf());
    pVSBlob->Release();
    if (FAILED(hr)) return false;

    hr = D3DCompileFromFile(L"Shaders/Sprite2D_PS.hlsl", nullptr, nullptr,
        "main", "ps_5_0", 0, 0, &pPSBlob, &pErrorBlob);
    if (FAILED(hr))
    {
        if (pErrorBlob)
        {
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

bool SpriteRenderer::CreateBuffers()
{
    // 頂点バッファ作成（四角形）
    SpriteVertex vertices[] =
    {
        { XMFLOAT3(-0.5f,  -0.5f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f,  -0.5f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(-0.5f, 0.5f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(0.5f, 0.5f, 0.0f), XMFLOAT2(1.0f, 1.0f) },
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = vertices;

    if (FAILED(m_device->CreateBuffer(&bd, &initData, m_vertexBuffer.GetAddressOf())))
        return false;

    // インデックスバッファ作成
    WORD indices[] = { 0, 1, 2, 2, 1, 3 };

    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(indices);
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    initData.pSysMem = indices;

    if (FAILED(m_device->CreateBuffer(&bd, &initData, m_indexBuffer.GetAddressOf())))
        return false;

    // 定数バッファ作成
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(ConstantBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_constantBuffer.GetAddressOf())))
        return false;

    return true;
}

void SpriteRenderer::Begin()
{
   
    // シェーダー設定
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

    // バッファ設定
    UINT stride = sizeof(SpriteVertex);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // サンプラー設定
    m_context->PSSetSamplers(0, 1, m_samplerState.GetAddressOf());

    // ブレンドステート設定
    float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
}

void SpriteRenderer::End()
{
    // 必要に応じてステートをリセット
}

void SpriteRenderer::DrawSprite(ID3D11ShaderResourceView* texture,
    float x, float y, float width, float height,
    float rotation, const XMFLOAT4& color)
{
    if (!texture)
    {
        OutputDebugString(L"texture is null\n");
        return;
    }

    // ワールド行列作成
    XMMATRIX scale = XMMatrixScaling(width, height, 1.0f);
    XMMATRIX rotationMat = XMMatrixRotationZ(rotation);
    XMMATRIX translation = XMMatrixTranslation(x + width / 2, y + height / 2, 0.0f);
    XMMATRIX world = scale * rotationMat * translation;

    // 定数バッファ更新
    ConstantBuffer cb;
    cb.World = XMMatrixTranspose(world);
    cb.Projection = XMMatrixTranspose(m_projectionMatrix);
    cb.Color = color;
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);

    // 定数バッファ設定
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // テクスチャ設定
    m_context->PSSetShaderResources(0, 1, &texture);

    // 描画
    m_context->DrawIndexed(6, 0, 0);

}
bool SpriteRenderer::LoadTexture(const wchar_t* filename, ID3D11ShaderResourceView** texture)
{
    return TextureLoader::LoadFromFile(m_device, filename, texture);
}
