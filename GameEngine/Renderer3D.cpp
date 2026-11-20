#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <algorithm>
#include "Renderer3D.h"
#include "RenderConfig.h"   // 論理座標の大きさ（DrawDissolveScreen）
#include "RenderConfig.h"

#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

Renderer3D::Renderer3D()
    : m_device(nullptr), m_context(nullptr)
    , m_screenWidth(0), m_screenHeight(0)
{
}

Renderer3D::~Renderer3D() {}

namespace {
    struct BlurCB { XMFLOAT2 texel; float radius; float pad; };
    struct BlurVtx { XMFLOAT2 pos; XMFLOAT2 uv; };
}

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
    // 加算合成：下の色に光を足す（重ねても下を隠さない）
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendAdd.GetAddressOf())))
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
    depthDesc.Width = g_renderWidth;
    depthDesc.Height = g_renderHeight;
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
    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&dsDesc, m_depthStencilState.GetAddressOf())))
        return false;

    D3D11_DEPTH_STENCIL_DESC dsNoWrite = {};
    dsNoWrite.DepthEnable = TRUE;
    dsNoWrite.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsNoWrite.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(m_device->CreateDepthStencilState(&dsNoWrite, m_depthNoWriteState.GetAddressOf())))
        return false;

    D3D11_DEPTH_STENCIL_DESC dsDisabled = {};
    dsDisabled.DepthEnable = FALSE;
    dsDisabled.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(m_device->CreateDepthStencilState(&dsDisabled, m_depthDisabledState.GetAddressOf())))
        return false;

    if (!InitFx()) return false;
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

    // 四隅を自由に決める板用（DrawQuad が描くたびに書き換える）
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.ByteWidth = sizeof(Vertex3D) * 4;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_quadVertexBuffer.GetAddressOf())))
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
    m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
}

void Renderer3D::End()
{
    // 深度バッファを外す
    ID3D11RenderTargetView* rtv = nullptr;
    m_context->OMGetRenderTargets(1, &rtv, nullptr);
    m_context->OMSetRenderTargets(1, &rtv, nullptr);
    if (rtv) rtv->Release();
}

void Renderer3D::SetDepthWrite(bool enabled)
{
    if (enabled)
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    else
        m_context->OMSetDepthStencilState(m_depthNoWriteState.Get(), 0);
}

void Renderer3D::SetAdditive(bool on)
{
    float bf[4] = {};
    m_context->OMSetBlendState(on ? m_blendAdd.Get() : m_blendState.Get(), bf, 0xffffffff);
}

void Renderer3D::SetDepthEnabled(bool enabled)
{
    if (enabled)
        m_context->OMSetDepthStencilState(m_depthStencilState.Get(), 0);
    else
        m_context->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);
}

void Renderer3D::DrawTile(ID3D11ShaderResourceView* texture,
    float x, float z, float size, const XMFLOAT4& color, float y)
{
    XMMATRIX world = XMMatrixScaling(size, 1.0f, size) *
        XMMatrixTranslation(x, y, z);

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

void Renderer3D::DrawTileEx(ID3D11ShaderResourceView* texture,
    float x, float z, float width, float depth,
    float rotationY, const XMFLOAT4& color ,float y)
{
    XMMATRIX world = XMMatrixScaling(width, 1.0f, depth) *
        XMMatrixRotationY(rotationY) *
        XMMatrixTranslation(x, y, z);

    ConstantBuffer3D cb;
    cb.World = XMMatrixTranspose(world);
    cb.View = XMMatrixTranspose(m_viewMatrix);
    cb.Projection = XMMatrixTranspose(m_projectionMatrix);
    cb.Color = color;
    cb.Extra.x = m_whiteMix;

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
    const XMFLOAT4& color,
    const XMFLOAT4& uvRect)
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
    cb.UvRect = uvRect;

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

void Renderer3D::DrawShadow(ID3D11ShaderResourceView* texture,
    float baseX, float baseZ, float width, float lenX, float lenZ,
    const XMFLOAT4& color)
{
    // 影の伸び方向(lenX,lenZ)。幅はその垂直方向へ
    float pl = sqrtf(lenX * lenX + lenZ * lenZ);
    float perpX = (pl > 1e-4f) ? -lenZ / pl : 1.0f;
    float perpZ = (pl > 1e-4f) ? lenX / pl : 0.0f;

    // 立てquad(ローカルy:-0.5=足元, +0.5=頭)を地面へシアー投影
    // 底辺はワールドX固定(斜め補正なし)、上端(頭)だけ(lenX,lenZ)へ倒す
    XMMATRIX world(
        width, 0.0f, 0.0f, 0.0f,     // ローカルx → 幅(ワールドX固定＝底は補正なし)
        lenX, 0.0f, lenZ, 0.0f,     // ローカルy(0=足元..1=頭) → 影の伸び(斜め)
        0.0f, 1.0f, 0.0f, 0.0f,     // ローカルz(未使用)
        baseX, -0.04f, baseZ, 1.0f);  // 足元(ローカルy=0)を基点

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

void Renderer3D::DrawStreak(ID3D11ShaderResourceView* texture, const XMFLOAT3& head, const XMFLOAT3& dir,
    float length, float width, const XMFLOAT4& color)
{
    XMMATRIX invView = XMMatrixInverse(nullptr, m_viewMatrix);
    XMVECTOR fwd = invView.r[2];                                         // カメラの前
    XMVECTOR d = XMLoadFloat3(&dir);
    if (XMVectorGetX(XMVector3LengthSq(d)) < 1e-8f) d = invView.r[1];    // 止まっている時は画面の上向き
    d = XMVector3Normalize(d);
    XMVECTOR side = XMVector3Cross(d, fwd);                              // 伸ばす向きとカメラの前の両方に直角＝幅の向き
    if (XMVectorGetX(XMVector3LengthSq(side)) < 1e-6f) side = invView.r[0];   // まっすぐカメラへ飛ぶ時はカメラの右
    side = XMVector3Normalize(side);

    XMMATRIX world;
    world.r[0] = XMVectorScale(side, width);                             // ローカルx（-0.5..0.5）→ 幅
    world.r[1] = XMVectorScale(d, length);                               // ローカルy（0=尾..1=頭）→ 長さ
    world.r[2] = fwd;                                                    // ローカルz（使わない）
    world.r[3] = XMVectorSetW(XMVectorSubtract(XMLoadFloat3(&head), world.r[1]), 1.0f);   // 尾を基点に

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

void Renderer3D::DrawQuad(ID3D11ShaderResourceView* texture,
    const XMFLOAT3& tl, const XMFLOAT3& tr, const XMFLOAT3& bl, const XMFLOAT3& br,
    const XMFLOAT4& uv, const XMFLOAT4& color)
{
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(m_context->Map(m_quadVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    Vertex3D* v = (Vertex3D*)ms.pData;   // 並びは床・板と同じ（左上・右上・左下・右下）
    v[0] = { tl, XMFLOAT2(uv.x, uv.y) };
    v[1] = { tr, XMFLOAT2(uv.z, uv.y) };
    v[2] = { bl, XMFLOAT2(uv.x, uv.w) };
    v[3] = { br, XMFLOAT2(uv.z, uv.w) };
    m_context->Unmap(m_quadVertexBuffer.Get(), 0);

    ConstantBuffer3D cb;
    cb.World = XMMatrixIdentity();   // 頂点がもうワールドの位置なので、動かさない
    cb.View = XMMatrixTranspose(m_viewMatrix);
    cb.Projection = XMMatrixTranspose(m_projectionMatrix);
    cb.Color = color;

    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, &texture);

    UINT stride = sizeof(Vertex3D);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->DrawIndexed(6, 0, 0);
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
}

bool Renderer3D::InitPostProcess()
{
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = m_screenWidth; td.Height = m_screenHeight;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_ppTex.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateRenderTargetView(m_ppTex.Get(), nullptr, m_ppRTV.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateShaderResourceView(m_ppTex.Get(), nullptr, m_ppSRV.GetAddressOf()))) return false;

    ComPtr<ID3DBlob> vs, ps, err;
    if (FAILED(D3DCompileFromFile(L"Shaders/Blur_VS.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, vs.GetAddressOf(), err.GetAddressOf()))) return false;
    m_device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, m_blurVS.GetAddressOf());
    if (FAILED(D3DCompileFromFile(L"Shaders/Blur_PS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, ps.GetAddressOf(), err.GetAddressOf()))) return false;
    m_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_blurPS.GetAddressOf());

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(BlurCB); bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_blurCB.GetAddressOf()))) return false;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    m_device->CreateSamplerState(&sd, m_ppSampler.GetAddressOf());

    D3D11_INPUT_ELEMENT_DESC il[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (FAILED(m_device->CreateInputLayout(il, 2, vs->GetBufferPointer(), vs->GetBufferSize(),
        m_blurLayout.GetAddressOf()))) return false;

    BlurVtx verts[6] = {
        {{-1.f,-1.f},{0.f,1.f}}, {{-1.f, 1.f},{0.f,0.f}}, {{ 1.f, 1.f},{1.f,0.f}},
        {{-1.f,-1.f},{0.f,1.f}}, {{ 1.f, 1.f},{1.f,0.f}}, {{ 1.f,-1.f},{1.f,1.f}},
    };
    D3D11_BUFFER_DESC vbd = {};
    vbd.ByteWidth = sizeof(verts); vbd.Usage = D3D11_USAGE_DEFAULT;
    vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vsd = {}; vsd.pSysMem = verts;
    if (FAILED(m_device->CreateBuffer(&vbd, &vsd, m_blurVB.GetAddressOf()))) return false;

    return true;
}

void Renderer3D::BeginOffscreen()
{
    if (!m_blurCB) return;
    m_context->OMGetRenderTargets(1, &m_savedRTV, nullptr);
    UINT vpn = 1; m_context->RSGetViewports(&vpn, &m_savedVP);              // 現在のVPを保存
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)m_screenWidth, (float)m_screenHeight, 0.0f, 1.0f };
    m_context->RSSetViewports(1, &vp);                                     // オフスクリーンに合わせる
    float clr[4] = { 0, 0, 0, 1 };
    m_context->ClearRenderTargetView(m_ppRTV.Get(), clr);
    m_context->OMSetRenderTargets(1, m_ppRTV.GetAddressOf(), nullptr);
}

void Renderer3D::EndOffscreenBlur(float radius)
{
    if (!m_blurCB) return;
    m_context->RSSetViewports(1, &m_savedVP);
    m_context->OMSetRenderTargets(1, &m_savedRTV, nullptr);   // バックバッファへ戻す

    BlurCB cb; cb.texel = XMFLOAT2(1.0f / m_screenWidth, 1.0f / m_screenHeight); cb.radius = radius; cb.pad = 0;
    m_context->UpdateSubresource(m_blurCB.Get(), 0, nullptr, &cb, 0, 0);

    m_context->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);
    m_context->IASetInputLayout(nullptr);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_blurVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_blurPS.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_blurCB.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, m_ppSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_ppSampler.GetAddressOf());
    m_context->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);
    m_context->IASetInputLayout(m_blurLayout.Get());
    UINT stride = sizeof(BlurVtx), offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_blurVB.GetAddressOf(), &stride, &offset);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_blurVS.Get(), nullptr, 0);
    m_context->PSSetShader(m_blurPS.Get(), nullptr, 0);
    m_context->PSSetConstantBuffers(0, 1, m_blurCB.GetAddressOf());
    m_context->PSSetShaderResources(0, 1, m_ppSRV.GetAddressOf());
    m_context->PSSetSamplers(0, 1, m_ppSampler.GetAddressOf());
    float bf[4] = {};
    m_context->OMSetBlendState(nullptr, bf, 0xffffffff);
    m_context->Draw(6, 0);

    ID3D11ShaderResourceView* nullSRV = nullptr;   // 入力を外す
    m_context->PSSetShaderResources(0, 1, &nullSRV);
    if (m_savedRTV) { m_savedRTV->Release(); m_savedRTV = nullptr; }
}

// エフェクト用シェーダー・その定数バッファ・くり返しのサンプラーを作る
bool Renderer3D::InitFx()
{
    ComPtr<ID3DBlob> ps, err;
    if (FAILED(D3DCompileFromFile(L"Shaders/Fx_PS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, ps.GetAddressOf(), err.GetAddressOf())))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, m_fxPS.GetAddressOf()))) return false;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(FxParams); bd.Usage = D3D11_USAGE_DEFAULT; bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_fxCB.GetAddressOf()))) return false;

    // 溶けるシェーダー（サンプラーと t1・t2 の使い方はエフェクト用シェーダーと同じ）
    ComPtr<ID3DBlob> dps;
    if (FAILED(D3DCompileFromFile(L"Shaders/Dissolve_PS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, dps.GetAddressOf(), err.ReleaseAndGetAddressOf())))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(dps->GetBufferPointer(), dps->GetBufferSize(), nullptr, m_dissolvePS.GetAddressOf()))) return false;
    bd.ByteWidth = sizeof(DissolveParams);
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_dissolveCB.GetAddressOf()))) return false;

    // 動きのブラー（t0 の絵だけを使う）
    ComPtr<ID3DBlob> mps;
    if (FAILED(D3DCompileFromFile(L"Shaders/MotionBlur_PS.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, mps.GetAddressOf(), err.ReleaseAndGetAddressOf())))
    {
        if (err) OutputDebugStringA((char*)err->GetBufferPointer());
        return false;
    }
    if (FAILED(m_device->CreatePixelShader(mps->GetBufferPointer(), mps->GetBufferSize(), nullptr, m_motionBlurPS.GetAddressOf()))) return false;
    bd.ByteWidth = sizeof(MotionBlurParams);
    if (FAILED(m_device->CreateBuffer(&bd, nullptr, m_motionBlurCB.GetAddressOf()))) return false;

    D3D11_SAMPLER_DESC sd = {};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;   // ノイズを流すため、端でくり返す
    sd.ComparisonFunc = D3D11_COMPARISON_NEVER; sd.MaxLOD = D3D11_FLOAT32_MAX;
    return SUCCEEDED(m_device->CreateSamplerState(&sd, m_samplerWrap.GetAddressOf()));
}

void Renderer3D::DrawFxBillboard(ID3D11ShaderResourceView* shape, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
    const FxParams& fx, float x, float y, float z, float size, const XMFLOAT4& color)
{
    m_context->UpdateSubresource(m_fxCB.Get(), 0, nullptr, &fx, 0, 0);
    m_context->PSSetConstantBuffers(1, 1, m_fxCB.GetAddressOf());
    ID3D11ShaderResourceView* extra[2] = { noise, ramp };
    m_context->PSSetShaderResources(1, 2, extra);
    m_context->PSSetSamplers(1, 1, m_samplerWrap.GetAddressOf());
    m_context->PSSetShader(m_fxPS.Get(), nullptr, 0);
    DrawBillboard(shape, x, y, z, size, size, 0.0f, color);    // 板・行列・形の画像（t0）はいつもの板と同じ
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);   // いつものシェーダーに戻す
}

void Renderer3D::DrawDissolveBillboard(ID3D11ShaderResourceView* texture, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
    const DissolveParams& dp, float x, float y, float z, float width, float height, float rotation,
    const XMFLOAT4& color, const XMFLOAT4& uvRect)
{
    m_context->UpdateSubresource(m_dissolveCB.Get(), 0, nullptr, &dp, 0, 0);
    m_context->PSSetConstantBuffers(1, 1, m_dissolveCB.GetAddressOf());
    ID3D11ShaderResourceView* extra[2] = { noise, ramp };
    m_context->PSSetShaderResources(1, 2, extra);
    m_context->PSSetSamplers(1, 1, m_samplerWrap.GetAddressOf());
    m_context->PSSetShader(m_dissolvePS.Get(), nullptr, 0);
    DrawBillboard(texture, x, y, z, width, height, rotation, color, uvRect);   // 板・行列・溶かす絵（t0）はいつもの板と同じ
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);   // いつものシェーダーに戻す
}

// 動きのブラー：本来の絵と、後ろへ伸ばした先までを全部覆う大きい板を描く。uv も絵の外まで広げ、外はシェーダーで透明にする
void Renderer3D::DrawMotionBlurBillboard(ID3D11ShaderResourceView* texture, float x, float y, float z, float width, float height,
    float rotation, const XMFLOAT4& color, const XMFLOAT4& uvRect, float blurX, float blurZ, float strength)
{
    // 板の右（ax）と上（ay）の向き（DrawBillboard と同じ回し方）
    XMMATRIX rot = XMMatrixRotationX(XMConvertToRadians(BILLBOARD_PITCH)) * XMMatrixRotationY(rotation);
    XMFLOAT3 ax, ay;
    XMStoreFloat3(&ax, XMVector3TransformNormal(XMVectorSet(1, 0, 0, 0), rot));
    XMStoreFloat3(&ay, XMVector3TransformNormal(XMVectorSet(0, 1, 0, 0), rot));
    // ワールドのブラーを、板の上の長さにする（右へ bx、上へ by）
    float bx = blurX * ax.x + blurZ * ax.z;
    float by = blurX * ay.x + blurZ * ay.z;
    // 本来の絵（右：-w/2 から w/2、上：0 から h）と、ブラーの先までを全部覆う範囲
    float x0 = (std::min)(-width * 0.5f, -width * 0.5f + bx), x1 = (std::max)(width * 0.5f, width * 0.5f + bx);
    float y0 = (std::min)(0.0f, by), y1 = (std::max)(height, height + by);
    // その範囲の uv（本来の絵が 0 から 1。上が v=0）
    float u0 = (x0 + width * 0.5f) / width, u1 = (x1 + width * 0.5f) / width;
    float v0 = 1.0f - y1 / height, v1 = 1.0f - y0 / height;
    XMFLOAT4 uvBig(uvRect.x + uvRect.z * u0, uvRect.y + uvRect.w * v0, uvRect.z * (u1 - u0), uvRect.w * (v1 - v0));

    MotionBlurParams mp;
    mp.blurUv = XMFLOAT2(bx / width * uvRect.z, -by / height * uvRect.w);   // 左右反転の絵でも合うように uvRect を掛ける
    mp.strength = strength;
    mp.rect = XMFLOAT4((std::min)(uvRect.x, uvRect.x + uvRect.z), (std::min)(uvRect.y, uvRect.y + uvRect.w),
                       (std::max)(uvRect.x, uvRect.x + uvRect.z), (std::max)(uvRect.y, uvRect.y + uvRect.w));
    m_context->UpdateSubresource(m_motionBlurCB.Get(), 0, nullptr, &mp, 0, 0);
    m_context->PSSetConstantBuffers(1, 1, m_motionBlurCB.GetAddressOf());
    m_context->PSSetShader(m_motionBlurPS.Get(), nullptr, 0);
    // 板の置き場所：真ん中を右へ (x0+x1)/2、足もとを上へ y0 だけずらす
    float cx = (x0 + x1) * 0.5f;
    DrawBillboard(texture, x + ax.x * cx + ay.x * y0, y + ax.y * cx + ay.y * y0, z + ax.z * cx + ay.z * y0,
        x1 - x0, y1 - y0, rotation, color, uvBig);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);   // いつものシェーダーに戻す
}

void Renderer3D::DrawDissolveScreen(ID3D11ShaderResourceView* texture, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
    const DissolveParams& dp, float x, float y, float w, float h, const XMFLOAT4& color)
{
    // 範囲の四隅を、画面の座標（左下 -1,-1 ～ 右上 1,1）にする
    float l = x / LOGICAL_WIDTH * 2.0f - 1.0f, r = (x + w) / LOGICAL_WIDTH * 2.0f - 1.0f;
    float t = 1.0f - y / LOGICAL_HEIGHT * 2.0f, b = 1.0f - (y + h) / LOGICAL_HEIGHT * 2.0f;
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(m_context->Map(m_quadVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    Vertex3D* v = (Vertex3D*)ms.pData;   // 左上・右上・左下・右下
    v[0] = { XMFLOAT3(l, t, 0.0f), XMFLOAT2(0.0f, 0.0f) };
    v[1] = { XMFLOAT3(r, t, 0.0f), XMFLOAT2(1.0f, 0.0f) };
    v[2] = { XMFLOAT3(l, b, 0.0f), XMFLOAT2(0.0f, 1.0f) };
    v[3] = { XMFLOAT3(r, b, 0.0f), XMFLOAT2(1.0f, 1.0f) };
    m_context->Unmap(m_quadVertexBuffer.Get(), 0);

    ConstantBuffer3D cb;
    cb.World = XMMatrixIdentity(); cb.View = XMMatrixIdentity(); cb.Projection = XMMatrixIdentity();
    cb.Color = color;
    cb.UvRect = XMFLOAT4(x / LOGICAL_WIDTH, y / LOGICAL_HEIGHT, w / LOGICAL_WIDTH, h / LOGICAL_HEIGHT);   // 同じ範囲を読む
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->UpdateSubresource(m_dissolveCB.Get(), 0, nullptr, &dp, 0, 0);

    // UI を描いている途中でも描けるように、板を描く設定を全部ここでする
    UINT stride = sizeof(Vertex3D), offset = 0;
    m_context->IASetInputLayout(m_inputLayout.Get());
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetShader(m_dissolvePS.Get(), nullptr, 0);
    ID3D11Buffer* cbs[2] = { m_constantBuffer.Get(), m_dissolveCB.Get() };
    m_context->PSSetConstantBuffers(0, 2, cbs);
    ID3D11ShaderResourceView* tex[3] = { texture, noise, ramp };
    m_context->PSSetShaderResources(0, 3, tex);
    ID3D11SamplerState* samp[2] = { m_samplerState.Get(), m_samplerWrap.Get() };
    m_context->PSSetSamplers(0, 2, samp);
    float bf[4] = {};
    m_context->OMSetBlendState(m_blendState.Get(), bf, 0xffffffff);
    m_context->OMSetDepthStencilState(m_depthDisabledState.Get(), 0);
    m_context->DrawIndexed(6, 0, 0);

    ID3D11ShaderResourceView* none[1] = { nullptr };   // 読んだ絵を外す（次にまた描き先にするため）
    m_context->PSSetShaderResources(0, 1, none);
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
}

void Renderer3D::DrawFxScreen(ID3D11ShaderResourceView* shape, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
    const FxParams& fx, const XMFLOAT4& color)
{
    // 画面いっぱいの板：頂点を最初から画面の座標（左下 -1,-1 ～ 右上 1,1）に置いて、ビュー・射影は掛けない
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(m_context->Map(m_quadVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) return;
    Vertex3D* v = (Vertex3D*)ms.pData;   // 並びは床・板と同じ（左上・右上・左下・右下）
    v[0] = { XMFLOAT3(-1.0f,  1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) };
    v[1] = { XMFLOAT3( 1.0f,  1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) };
    v[2] = { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) };
    v[3] = { XMFLOAT3( 1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) };
    m_context->Unmap(m_quadVertexBuffer.Get(), 0);

    ConstantBuffer3D cb;
    cb.World = XMMatrixIdentity();
    cb.View = XMMatrixIdentity();
    cb.Projection = XMMatrixIdentity();
    cb.Color = color;
    m_context->UpdateSubresource(m_constantBuffer.Get(), 0, nullptr, &cb, 0, 0);
    m_context->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->PSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // エフェクト用シェーダーと、その数字・3枚の画像（DrawFxBillboard と同じ）
    m_context->UpdateSubresource(m_fxCB.Get(), 0, nullptr, &fx, 0, 0);
    m_context->PSSetConstantBuffers(1, 1, m_fxCB.GetAddressOf());
    ID3D11ShaderResourceView* tex[3] = { shape, noise, ramp };
    m_context->PSSetShaderResources(0, 3, tex);
    m_context->PSSetSamplers(1, 1, m_samplerWrap.GetAddressOf());
    m_context->PSSetShader(m_fxPS.Get(), nullptr, 0);

    SetDepthEnabled(false);   // 画面に貼るだけなので、奥行きは見ない
    UINT stride = sizeof(Vertex3D);
    UINT offset = 0;
    m_context->IASetVertexBuffers(0, 1, m_quadVertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->DrawIndexed(6, 0, 0);
    m_context->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &stride, &offset);
    m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);   // いつものシェーダーに戻す
    SetDepthEnabled(true);
}
