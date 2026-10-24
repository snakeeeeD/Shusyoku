#pragma once
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

using Microsoft::WRL::ComPtr;

class TextRenderer
{
public:
    TextRenderer();
    ~TextRenderer();

    bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
        IDXGISwapChain* swapChain);
    void Shutdown();

    // テキスト描画
    void DrawText(const wchar_t* text,
        float x, float y,
        float size = 24.0f,
        D2D1_COLOR_F color = D2D1::ColorF(D2D1::ColorF::White),
        float rotation = 0.0f, float pivotX = 0.0f, float pivotY = 0.0f);

    // 縦方向グラデーション（上端α aTop → 下端α aBot）
    void DrawTextGradientV(const wchar_t* text, float x, float y, float size,
        D2D1_COLOR_F color, float aTop, float aBot);

    void DrawOutlinedText(const wchar_t* text,
        float x, float y, float size,
        D2D1_COLOR_F color,
        D2D1_COLOR_F outline = D2D1::ColorF(D2D1::ColorF::Black),
        float thickness = 2.0f,
        float rotation = 0.0f, float pivotX = 0.0f, float pivotY = 0.0f);   // 手札の傾き用

    // 中央揃え：centerX を中心に幅 width の枠で描く（はみ出す行は折り返し、各行が中央に揃う）
    // height > 0 なら、y~y+height の縦の中央にも揃える
    void DrawTextCentered(const wchar_t* text, float centerX, float y, float width,
        float size = 24.0f, D2D1_COLOR_F color = D2D1::ColorF(D2D1::ColorF::White), float height = 0.0f);

    // 縁取り＋中央揃え（バナーの文字など）
    void DrawOutlinedTextCentered(const wchar_t* text, float centerX, float y, float width, float height,
        float size, D2D1_COLOR_F color,
        D2D1_COLOR_F outline = D2D1::ColorF(D2D1::ColorF::Black), float thickness = 2.0f);

    // 縁取りの自動付与（既定ON）。付けたくない画面だけ一時的にOFFにする
    void SetAutoOutline(bool on) { m_autoOutline = on; }

    void Begin();
    void End();

private:
    ComPtr<IDWriteTextFormat> MakeFormat(float size, bool centerH, bool centerV);
    void AutoOutline(const D2D1_COLOR_F& color, float size, D2D1_COLOR_F& outline, float& thickness) const;
    void DrawLayout(const wchar_t* text, IDWriteTextFormat* format, const D2D1_RECT_F& rect,
        ID2D1Brush* fill, ID2D1Brush* outline, float thickness,
        float rotation, float pivotX, float pivotY);

    ComPtr<ID2D1Factory>        m_d2dFactory;
    ComPtr<ID2D1RenderTarget>   m_d2dRenderTarget;
    ComPtr<IDWriteFactory>      m_dwriteFactory;
    ComPtr<IDWriteTextFormat>   m_textFormat;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<ID2D1SolidColorBrush> m_outlineBrush;   // 縁取り用
    bool m_autoOutline = true;
};