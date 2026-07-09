#include "TextRenderer.h"

TextRenderer::TextRenderer() {}
TextRenderer::~TextRenderer() { Shutdown(); }

bool TextRenderer::Init(ID3D11Device* device, ID3D11DeviceContext* context,
    IDXGISwapChain* swapChain)
{
    HRESULT hr;

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        m_d2dFactory.GetAddressOf());
    if (FAILED(hr)) return false; 


    ComPtr<IDXGISurface> dxgiSurface;
    hr = swapChain->GetBuffer(0, IID_PPV_ARGS(dxgiSurface.GetAddressOf()));
    if (FAILED(hr)) return false; 


    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    hr = m_d2dFactory->CreateDxgiSurfaceRenderTarget(
        dxgiSurface.Get(), &props, m_d2dRenderTarget.GetAddressOf());
    if (FAILED(hr)) return false;

    // DirectWriteファクトリ作成
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    // デフォルトフォント作成
    hr = m_dwriteFactory->CreateTextFormat(
        L"Arial", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        24.0f, L"ja-JP",
        m_textFormat.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    // ブラシ作成
    hr = m_d2dRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::White),
        m_brush.GetAddressOf()
    );
    if (FAILED(hr)) return false;

    return true;
}

void TextRenderer::Shutdown()
{
    // ComPtrが自動解放
}

void TextRenderer::Begin()
{
    m_d2dRenderTarget->BeginDraw();
}

void TextRenderer::End()
{
    m_d2dRenderTarget->EndDraw();
}

void TextRenderer::DrawText(const wchar_t* text,
    float x, float y,
    float size,
    D2D1_COLOR_F color)
{
    // フォントサイズが違う場合は新しいフォーマットを作成
    ComPtr<IDWriteTextFormat> format;
    m_dwriteFactory->CreateTextFormat(
        L"Arial", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        size, L"ja-JP",
        format.GetAddressOf()
    );

    m_brush->SetColor(color);

    D2D1_RECT_F rect = D2D1::RectF(x, y, x + 500.0f, y + 200.0f);
    m_d2dRenderTarget->DrawTextW(text, (UINT32)wcslen(text),
        format.Get(), rect, m_brush.Get());
}