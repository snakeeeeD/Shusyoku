#pragma once
#include "SpriteRenderer.h"
#include "TextureManager.h"
#include <DirectXMath.h>

class UiWindow
{
public:
    static void Draw(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        float x, float y, float w, float h,
        const DirectX::XMFLOAT4& tint = DirectX::XMFLOAT4(1, 1, 1, 1))
    {
        using namespace DirectX;
        ID3D11ShaderResourceView* tex = TextureManager::Get("ui_window");
        if (!tex)   // 未ロード時フォールバック
        {
            sr->DrawSprite(white, x, y, w, h, 0.0f, XMFLOAT4(0.09f, 0.10f, 0.16f, 0.95f));
            return;
        }
        const float TEX = 48.0f, CORNER = 16.0f;
        const float u = CORNER / TEX;
        float bw = (CORNER < w * 0.5f) ? CORNER : w * 0.5f;
        float bh = (CORNER < h * 0.5f) ? CORNER : h * 0.5f;
        float xs[3] = { x, x + bw, x + w - bw };
        float ws[3] = { bw, w - bw * 2, bw };
        float ys[3] = { y, y + bh, y + h - bh };
        float hs[3] = { bh, h - bh * 2, bh };
        float us[3] = { 0.0f, u, 1.0f - u };
        float uw[3] = { u, 1.0f - u * 2, u };
        for (int r = 0; r < 3; r++)
            for (int c = 0; c < 3; c++)
                sr->DrawSprite(tex, xs[c], ys[r], ws[c], hs[r], 0.0f, tint,
                    XMFLOAT4(us[c], us[r], uw[c], uw[r]));
    }

    static void Button(SpriteRenderer* sr, ID3D11ShaderResourceView* white,
        float x, float y, float w, float h, const DirectX::XMFLOAT4& color)
    {
        Draw(sr, white, x, y, w, h);
        sr->DrawSprite(white, x + 6, y + 6, w - 12, h - 12, 0.0f, color);
    }
};