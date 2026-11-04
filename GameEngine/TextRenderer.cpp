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

    LoadFonts();

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

// 同梱フォントを登録する（Windows 10 以降の FontSet）。失敗したら Arial のまま
bool TextRenderer::LoadFonts()
{
    ComPtr<IDWriteFactory5> f5;
    if (FAILED(m_dwriteFactory.As(&f5))) return false;
    ComPtr<IDWriteFontSetBuilder1> builder;
    if (FAILED(f5->CreateFontSetBuilder(builder.GetAddressOf()))) return false;
    const wchar_t* files[] = { L"Assets/Font/NotoSerifJP-Bold.ttf", L"Assets/Font/MPLUS1p-Bold.ttf" };
    for (auto f : files)
    {
        wchar_t full[MAX_PATH];
        if (!GetFullPathNameW(f, MAX_PATH, full, nullptr)) return false;
        ComPtr<IDWriteFontFile> file;
        if (FAILED(f5->CreateFontFileReference(full, nullptr, file.GetAddressOf()))) return false;
        if (FAILED(builder->AddFontFile(file.Get()))) return false;
    }
    ComPtr<IDWriteFontSet> set;
    if (FAILED(builder->CreateFontSet(set.GetAddressOf()))) return false;
    ComPtr<IDWriteFontCollection1> col;
    if (FAILED(f5->CreateFontCollectionFromFontSet(set.Get(), col.GetAddressOf()))) return false;
    UINT32 idx; BOOL found = FALSE;
    col->FindFamilyName(L"M PLUS 1p", &idx, &found);   // 名前が合わなければ使わない（別フォントに化けるのを防ぐ）
    if (!found) return false;
    m_fonts = col;
    return true;
}

// 描いた時の大きさ（論理座標）
D2D1_SIZE_F TextRenderer::Measure(const wchar_t* text, float size, FontRole role, float maxWidth)
{
    auto format = MakeFormat(size, false, false, role);
    ComPtr<IDWriteTextLayout> layout;
    if (!text || !format || FAILED(m_dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text), format.Get(),
        maxWidth, 4000.0f, layout.GetAddressOf()))) return D2D1::SizeF(0, 0);
    DWRITE_TEXT_METRICS m; layout->GetMetrics(&m);
    return D2D1::SizeF(m.widthIncludingTrailingWhitespace, m.height);
}

// 真っ白は少し温かい白に（金縁の UI になじませる）
D2D1_COLOR_F TextRenderer::Warm(D2D1_COLOR_F c)
{
    if (c.r >= 0.999f && c.g >= 0.999f && c.b >= 0.999f) { c.r = 0.95f; c.g = 0.9f; c.b = 0.8f; }
    return c;
}

// 書式（フォント・大きさ・揃え）を作る
ComPtr<IDWriteTextFormat> TextRenderer::MakeFormat(float size, bool centerH, bool centerV, FontRole role)
{
    ComPtr<IDWriteTextFormat> format;
    const wchar_t* family = !m_fonts ? L"Arial"
        : (role == FontRole::Heading ? L"Noto Serif JP" : L"M PLUS 1p");
    m_dwriteFactory->CreateTextFormat(family, m_fonts.Get(),
        m_fonts ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
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
    float rotation, float pivotX, float pivotY, const std::vector<TextSpan>* spans)
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

    // 縁取りを描いた後で範囲の色を付ける（先に付けると縁取りまで色が付く）
    std::vector<ComPtr<ID2D1SolidColorBrush>> spanBrushes;
    if (spans)
        for (auto& s : *spans)
        {
            ComPtr<ID2D1SolidColorBrush> b;
            m_d2dRenderTarget->CreateSolidColorBrush(Warm(s.color), b.GetAddressOf());
            if (!b) continue;
            layout->SetDrawingEffect(b.Get(), DWRITE_TEXT_RANGE{ s.start, s.length });
            spanBrushes.push_back(b);
        }
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
    auto format = MakeFormat(size, false, false, m_role);
    m_brush->SetColor(Warm(color));
    m_outlineBrush->SetColor(outline);
    DrawLayout(text, format.Get(), D2D1::RectF(x, y, x + 500.0f, y + 200.0f),
        m_brush.Get(), m_outlineBrush.Get(), thickness, rotation, pivotX, pivotY);
}

void TextRenderer::DrawTextSpans(const wchar_t* text, float x, float y, float width, float size,
    D2D1_COLOR_F color, const std::vector<TextSpan>& spans)
{
    D2D1_COLOR_F ol; float th;
    AutoOutline(color, size, ol, th);
    auto format = MakeFormat(size, false, false, m_role);
    m_brush->SetColor(Warm(color));
    m_outlineBrush->SetColor(ol);
    DrawLayout(text, format.Get(), D2D1::RectF(x, y, x + width, y + 4000.0f),
        m_brush.Get(), m_outlineBrush.Get(), th, 0.0f, 0.0f, 0.0f, &spans);
}

int TextRenderer::HitTestSpans(const wchar_t* text, float x, float y, float width, float size,
    const std::vector<TextSpan>& spans, float mx, float my)
{
    if (!text || spans.empty()) return -1;
    auto format = MakeFormat(size, false, false, m_role);
    ComPtr<IDWriteTextLayout> layout;
    if (!format || FAILED(m_dwriteFactory->CreateTextLayout(text, (UINT32)wcslen(text), format.Get(),
        width, 4000.0f, layout.GetAddressOf()))) return -1;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hm;
    layout->HitTestPoint(mx - x, my - y, &trailing, &inside, &hm);
    if (!inside) return -1;
    for (int i = 0; i < (int)spans.size(); i++)
        if (hm.textPosition >= spans[i].start && hm.textPosition < spans[i].start + spans[i].length) return i;
    return -1;
}

void TextRenderer::DrawTextBox(const wchar_t* text, float x, float y, float width, float size, D2D1_COLOR_F color)
{
    D2D1_COLOR_F ol; float th;
    AutoOutline(color, size, ol, th);
    auto format = MakeFormat(size, false, false, m_role);
    m_brush->SetColor(Warm(color));
    m_outlineBrush->SetColor(ol);
    DrawLayout(text, format.Get(), D2D1::RectF(x, y, x + width, y + 4000.0f),
        m_brush.Get(), m_outlineBrush.Get(), th, 0.0f, 0.0f, 0.0f);
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
    // 枠の中央に置く文字（ボタン・バナー・タイトル）は見出しのフォント
    auto format = MakeFormat(size, true, height > 0.0f, height > 0.0f ? FontRole::Heading : m_role);
    m_brush->SetColor(Warm(color));
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
    DrawLayout(text, MakeFormat(size, false, false, m_role).Get(), D2D1::RectF(x, y, x + 500.0f, y + 200.0f),
        fill.Get(), edge.Get(), t, 0.0f, 0.0f, 0.0f);
}