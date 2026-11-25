#pragma once
#include <d3d11.h>
#include <d2d1.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
class TextRenderer;
class Renderer3D;

// カードを焼いて消す（廃棄）。
// カードの枠・絵（スプライト）と文字（Direct2D）は別々に描いているので、そのままでは文字だけ溶かせない。
// そこで、いったん画面と同じ大きさの別の絵（オフスクリーン）にカードを丸ごと描いて1枚の絵にまとめ、
// その1枚を溶けるシェーダー（Dissolve_PS）で画面に描く。文字も絵の一部なので、同じ境目で一緒に溶ける。
class CardBurn
{
public:
    void Init(ID3D11Device* device, ID3D11DeviceContext* context) { m_device = device; m_context = context; }
    // ここから EndCapture までの描画（スプライト・文字）を、別の絵に描く（最初に透明で塗りつぶす）
    bool BeginCapture(TextRenderer* text);
    void EndCapture(TextRenderer* text);   // 画面に戻す
    // まとめた絵のうち (x, y, w, h)（論理座標）の範囲を、溶けるシェーダーで画面に描く
    void Draw(Renderer3D* r, float x, float y, float w, float h, float progress, float seed);

private:
    bool EnsureTarget(TextRenderer* text);   // 別の絵を（まだ無い・大きさが違えば）作る

    ID3D11Device* m_device = nullptr;
    ID3D11DeviceContext* m_context = nullptr;
    ComPtr<ID3D11Texture2D> m_tex;            // 画面と同じ大きさの別の絵
    ComPtr<ID3D11RenderTargetView> m_rtv;     // スプライトの描き先
    ComPtr<ID3D11ShaderResourceView> m_srv;   // 溶かすときに読む
    ComPtr<ID2D1RenderTarget> m_textTarget;   // 文字の描き先
    int m_w = 0, m_h = 0;
    ComPtr<ID3D11RenderTargetView> m_savedRTV;   // 戻す先（画面）
    ComPtr<ID3D11DepthStencilView> m_savedDSV;
    bool m_capturing = false;
};
