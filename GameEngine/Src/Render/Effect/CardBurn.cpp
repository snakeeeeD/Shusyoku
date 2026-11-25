#include "CardBurn.h"
#include "TextRenderer.h"
#include "Renderer3D.h"
#include "RenderConfig.h"      // 実際の解像度（g_renderWidth / g_renderHeight）
#include "EffectDataBase.h"    // 溶ける見た目の材質（fx_dissolve。敵を倒したときと同じ）
#include "TextureManager.h"

bool CardBurn::EnsureTarget(TextRenderer* text)
{
    if (m_tex && m_w == g_renderWidth && m_h == g_renderHeight) return true;
    m_tex.Reset(); m_rtv.Reset(); m_srv.Reset(); m_textTarget.Reset();
    m_w = g_renderWidth; m_h = g_renderHeight;

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = m_w; td.Height = m_h;
    td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;   // 文字（Direct2D）も描けるように、画面と同じ並び
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_device->CreateTexture2D(&td, nullptr, m_tex.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateRenderTargetView(m_tex.Get(), nullptr, m_rtv.GetAddressOf()))) return false;
    if (FAILED(m_device->CreateShaderResourceView(m_tex.Get(), nullptr, m_srv.GetAddressOf()))) return false;
    m_textTarget = text->CreateTextureTarget(m_tex.Get());
    return m_textTarget != nullptr;
}

bool CardBurn::BeginCapture(TextRenderer* text)
{
    if (!EnsureTarget(text)) return false;
    m_context->OMGetRenderTargets(1, m_savedRTV.ReleaseAndGetAddressOf(), m_savedDSV.ReleaseAndGetAddressOf());
    float clear[4] = { 0, 0, 0, 0 };
    m_context->ClearRenderTargetView(m_rtv.Get(), clear);
    m_context->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);   // ビューポートは画面と同じ大きさなので、そのまま
    text->SetTarget(m_textTarget.Get());
    m_capturing = true;
    return true;
}

void CardBurn::EndCapture(TextRenderer* text)
{
    if (!m_capturing) return;
    text->SetTarget(nullptr);
    ID3D11RenderTargetView* rtv = m_savedRTV.Get();
    m_context->OMSetRenderTargets(1, &rtv, m_savedDSV.Get());
    m_savedRTV.Reset(); m_savedDSV.Reset();
    m_capturing = false;
}

void CardBurn::Draw(Renderer3D* r, float x, float y, float w, float h, float progress, float seed)
{
    const FxMaterial* mat = EffectDataBase::GetMaterial("fx_dissolve");
    if (!mat || !m_srv) return;
    DissolveParams dp;
    dp.progress = progress;
    dp.noiseScale = mat->noiseScale; dp.glow = mat->glow;
    dp.seed = seed;
    dp.premultiplied = 1.0f;   // まとめた絵は、色に透明度が掛かった形で入っている
    r->DrawDissolveScreen(m_srv.Get(), TextureManager::Get(mat->noise), TextureManager::Get(mat->ramp), dp,
        x, y, w, h, XMFLOAT4(1, 1, 1, 1));
}
