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

    void Begin();
    void End();

private:
    ComPtr<ID2D1Factory>        m_d2dFactory;
    ComPtr<ID2D1RenderTarget>   m_d2dRenderTarget;
    ComPtr<IDWriteFactory>      m_dwriteFactory;
    ComPtr<IDWriteTextFormat>   m_textFormat;
    ComPtr<ID2D1SolidColorBrush> m_brush;
};