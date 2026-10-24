#include "TextRenderer.h"
#include "RenderConfig.h"

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
    hr = m_d2dRenderTarget->CreateSolidColorBrush(
        D2D1::ColorF(D2D1::ColorF::Black),
        m_outlineBrush.GetAddressOf()
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

// 書式（フォント・大きさ・揃え）を作る
ComPtr<IDWriteTextFormat> TextRenderer::MakeFormat(float size, bool centerH, bool centerV)
{
    ComPtr<IDWriteTextFormat> format;
    m_dwriteFactory->CreateTextFormat(L"Arial", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size, L"ja-JP", format.GetAddressOf());
    if (format)
    {
        if (centerH) format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);              // 各行を枠の横中央に
        if (centerV) format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);    // 枠の縦中央に
    }
    return format;
}

// 自動の縁取り：文字色のαに合わせた黒。暗い文字（明るい紙の上のインク文字など）には付けない
void TextRenderer::AutoOutline(const D2D1_COLOR_F& color, float size, D2D1_COLOR_F& outline, float& thickness) const
{
    outline = D2D1::ColorF(0.0f, 0.0f, 0.0f, color.a * 0.85f);
    float luma = color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
    if (!m_autoOutline || luma < 0.35f) { thickness = 0.0f; return; }
    thickness = (size * 0.06f > 1.0f) ? size * 0.06f : 1.0f;   // 大きい文字ほど太く
}

// 共通の描画：レイアウトを1回だけ作り、縁取り（8方向）→本体の順に描く
void TextRenderer::DrawLayout(const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
    ID2D1Brush* fill, ID2D1Brush* outline, float thickness,
    float rotation, float pivotX, float pivotY)
{
    if (!text || !format) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(m_dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text), format,
        rect.right - rect.left, rect.bottom - rect.top, layout.GetAddressOf()))) return;

    float sx = (float)g_renderWidth / (float)LOGICAL_WIDTH;
    float sy = (float)g_renderHeight / (float)LOGICAL_HEIGHT;
    D2D1::Matrix3x2F xf = D2D1::Matrix3x2F::Scale(sx, sy);   // 論理座標 → 実解像度
    if (rotation != 0.0f)
        xf = D2D1::Matrix3x2F::Rotation(rotation * 180.0f / 3.14159f, D2D1::Point2F(pivotX, pivotY)) * xf;   // 論理で回転→拡大
    m_d2dRenderTarget->SetTransform(xf);

    if (outline && thickness > 0.0f)
        for (int dx = -1; dx <= 1; dx++)
            for (int dy = -1; dy <= 1; dy++)
                if (dx || dy)
                    m_d2dRenderTarget->DrawTextLayout(
                        D2D1::Point2F(rect.left + dx * thickness, rect.top + dy * thickness), layout.Get(), outline);
    m_d2dRenderTarget->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.Get(), fill);

    m_d2dRenderTarget->SetTransform(D2D1::Matrix3x2F::Identity());
}

void TextRenderer::DrawText(const wchar_t* text,
    float x, float y, float size, D2D1_COLOR_F color,
    float rotation, float pivotX, float pivotY)
{
    D2D1_COLOR_F ol; float t;
    AutoOutline(color, size, ol, t);
    DrawOutlinedText(text, x, y, size, color, ol, t, rotation, pivotX, pivotY);
}

void TextRenderer::DrawOutlinedText(const wchar_t* text,
    float x, float y, float size,
    D2D1_COLOR_F color, D2D1_COLOR_F outline, float thickness,
    float rotation, float pivotX, float pivotY)
{
    auto format = MakeFormat(size, false, false);
    m_brush->SetColor(color);
    m_outlineBrush->SetColor(outline);
    DrawLayout(text, format.Get(), D2D1::RectF(x, y, x + 500.0f, y + 200.0f),
        m_brush.Get(), m_outlineBrush.Get(), thickness, rotation, pivotX, pivotY);
}

void TextRenderer::DrawTextCentered(const wchar_t* text, float centerX, float y, float width,
    float size, D2D1_COLOR_F color, float height)
{
    D2D1_COLOR_F ol; float t;
    AutoOutline(color, size, ol, t);
    DrawOutlinedTextCentered(text, centerX, y, width, height, size, color, ol, t);
}

void TextRenderer::DrawOutlinedTextCentered(const wchar_t* text, float centerX, float y, float width, float height,
    float size, D2D1_COLOR_F color, D2D1_COLOR_F outline, float thickness)
{
    auto format = MakeFormat(size, true, height > 0.0f);
    m_brush->SetColor(color);
    m_outlineBrush->SetColor(outline);
    float bottom = (height > 0.0f) ? y + height : y + 400.0f;
    DrawLayout(text, format.Get(), D2D1::RectF(centerX - width / 2.0f, y, centerX + width / 2.0f, bottom),
        m_brush.Get(), m_outlineBrush.Get(), thickness, 0.0f, 0.0f, 0.0f);
}

void TextRenderer::DrawTextGradientV(const wchar_t* text, float x, float y, float size,
    D2D1_COLOR_F color, float aTop, float aBot)
{
    // 本体と縁取りで同じ上下グラデーション（縁取りも一緒に消えていく）
    auto makeGrad = [&](D2D1_COLOR_F c, ComPtr<ID2D1LinearGradientBrush>& out) -> bool {
        D2D1_GRADIENT_STOP stops[2] = {
            { 0.0f, D2D1::ColorF(c.r, c.g, c.b, c.a * aTop) },
            { 1.0f, D2D1::ColorF(c.r, c.g, c.b, c.a * aBot) } };
        ComPtr<ID2D1GradientStopCollection> coll;
        if (FAILED(m_d2dRenderTarget->CreateGradientStopCollection(stops, 2, coll.GetAddressOf()))) return false;
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES gp = { D2D1::Point2F(x, y), D2D1::Point2F(x, y + size) };
        return SUCCEEDED(m_d2dRenderTarget->CreateLinearGradientBrush(gp, coll.Get(), out.GetAddressOf()));
        };

    D2D1_COLOR_F ol; float t;
    AutoOutline(color, size, ol, t);
    ComPtr<ID2D1LinearGradientBrush> fill, edge;
    if (!makeGrad(color, fill)) return;
    if (t > 0.0f && !makeGrad(ol, edge)) t = 0.0f;
    DrawLayout(text, MakeFormat(size, false, false).Get(), D2D1::RectF(x, y, x + 500.0f, y + 200.0f),
        fill.Get(), edge.Get(), t, 0.0f, 0.0f, 0.0f);
}