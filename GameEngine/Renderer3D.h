#pragma once
#include<d3d11.h>
#include<DirectXMath.h>
#include<wrl/client.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex3D
{
	XMFLOAT3 Pos;
	XMFLOAT2 Tex;
};

struct ConstantBuffer3D

{
	XMMATRIX World;
	XMMATRIX View;
	XMMATRIX Projection;
	XMFLOAT4 Color;
    XMFLOAT4 UvRect = { 0, 0, 1, 1 };
    XMFLOAT4 Extra = { 0, 0, 0, 0 };   // x=白を混ぜる量（0～1）
};

// エフェクト用シェーダー（Fx_PS.hlsl）に渡す数字。シェーダーの FxParams と同じ並び（16バイト単位）
struct FxParams
{
    float life = 0.0f;           // 寿命の進み具合（0→1）
    float erodeStart = 0.5f;     // この進み具合から消え始める
    float erodeEnd = 1.0f;       // ここで消えきる
    float glow = 1.0f;           // 色を何倍に光らせるか
    XMFLOAT2 scroll = { 0, 0 };  // ノイズを流す向きと速さ
    float noiseScale = 1.0f;     // ノイズの細かさ
    float time = 0.0f;           // 経過時間（秒）
    float seed = 0.0f;           // 粒ごとの乱数
    float appear = 0.0f;         // 出てくるのにかかる寿命の割合。0なら最初から全部出ている。0より大きいと、形の画像の G が小さい所から順に出てくる
    float pad[2] = {};
};

// 溶けるシェーダー（Dissolve_PS.hlsl）に渡す数字。シェーダーの DissolveParams と同じ並び（32バイト）
struct DissolveParams
{
    float progress = 0.0f;       // 溶けた割合（0→1）
    float edge = 0.07f;          // 光る境目の幅
    float noiseScale = 1.6f;     // ノイズの細かさ
    float glow = 1.0f;           // 境目の明るさ
    float bias = 0.22f;          // 上から溶ける強さ（0＝ノイズだけ）
    float seed = 0.0f;           // 模様をずらす乱数
    float white = 0.0f;          // 白を混ぜる量（とどめの一瞬）
    float premultiplied = 0.0f;  // 1＝絵の色に透明度がもう掛かっている（オフスクリーンにまとめた絵）
};

class Renderer3D
{
public:
	Renderer3D();
	~Renderer3D();

	bool Init(ID3D11Device* device, ID3D11DeviceContext* context,
		int screenWidth, int screenHeight);

    void Begin();
    void End();

    // 床タイル描画
    void DrawTile(ID3D11ShaderResourceView* texture,
        float x, float z, float size,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1), float y = 0.0f);

    void DrawTileEx(ID3D11ShaderResourceView* texture,
        float x, float z, float width, float depth,
        float rotationY,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1),
        float y = 0.03f);

    // カメラ設定
    void SetCamera(XMFLOAT3 pos, XMFLOAT3 target, XMFLOAT3 up);

    XMMATRIX GetViewMatrix() const { return m_viewMatrix; }
    XMMATRIX GetProjectionMatrix() const { return m_projectionMatrix; }

    void DrawBillboard(ID3D11ShaderResourceView* texture,
        float x, float y, float z,
        float width, float height,
        float rotation,
        const XMFLOAT4& color = XMFLOAT4(1, 1, 1, 1),
        const XMFLOAT4& uvRect = XMFLOAT4(0, 0, 1, 1));

    void DrawShadow(ID3D11ShaderResourceView* texture,
        float baseX, float baseZ, float width, float lenX, float lenZ,
        const XMFLOAT4& color = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.3f));

    // 速さの向きに伸ばした板（火花・しずく）。head＝頭（粒の位置）、尾は dir の逆へ length。板の面はカメラに向ける
    void DrawStreak(ID3D11ShaderResourceView* texture, const XMFLOAT3& head, const XMFLOAT3& dir,
        float length, float width, const XMFLOAT4& color);

    // 四隅を自由に決めた板（台形など）。tl/tr/bl/br＝左上・右上・左下・右下（ワールド）、uv＝(左のu, 上のv, 右のu, 下のv)
    void DrawQuad(ID3D11ShaderResourceView* texture,
        const XMFLOAT3& tl, const XMFLOAT3& tr, const XMFLOAT3& bl, const XMFLOAT3& br,
        const XMFLOAT4& uv, const XMFLOAT4& color);

    // エフェクト用シェーダーで板を描く（t0=形 t1=ノイズ t2=色の帯）。位置の基準と傾きは DrawBillboard と同じ
    void DrawFxBillboard(ID3D11ShaderResourceView* shape, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
        const FxParams& fx, float x, float y, float z, float size, const XMFLOAT4& color);
    // 溶けるシェーダーで板を描く（t0=溶かす絵 t1=ノイズ t2=色の帯）。位置・大きさ・uv は DrawBillboard と同じ
    void DrawDissolveBillboard(ID3D11ShaderResourceView* texture, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
        const DissolveParams& dp, float x, float y, float z, float width, float height, float rotation,
        const XMFLOAT4& color, const XMFLOAT4& uvRect = XMFLOAT4(0, 0, 1, 1));
    // 溶けるシェーダーで、画面の (x, y, w, h)（論理座標）に描く。texture は画面と同じ大きさの絵で、同じ範囲を読む。
    // UI を描いている途中でも呼べるように、必要な設定はこの中で全部する
    void DrawDissolveScreen(ID3D11ShaderResourceView* texture, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
        const DissolveParams& dp, float x, float y, float w, float h, const XMFLOAT4& color);
    // エフェクト用シェーダーで、画面いっぱいに1枚描く（Begin～End の間で呼ぶ。奥行きは見ない）
    void DrawFxScreen(ID3D11ShaderResourceView* shape, ID3D11ShaderResourceView* noise, ID3D11ShaderResourceView* ramp,
        const FxParams& fx, const XMFLOAT4& color);

    // HPバー用
    static constexpr float BILLBOARD_PITCH = -60.0f;        // ビルボードの傾き角度
    static float GetBillboardPitch() { return BILLBOARD_PITCH; }

    void SetDepthWrite(bool enabled);
    void SetDepthEnabled(bool enabled);
    void SetAdditive(bool on);   // 加算合成（下の模様を隠さず、上から光らせる描画用）
    void SetWhiteMix(float v) { m_whiteMix = v; }   // DrawTileEx に効く（使ったら0に戻す）

    bool InitPostProcess();
    void BeginOffscreen();               // 以降の描画をオフスクリーンへ
    void EndOffscreenBlur(float radius); // ぼかしてバックバッファへ合成

private:
    bool CreateShaders();
    bool CreateBuffers();
    bool InitFx();   // エフェクト用シェーダー・定数バッファ・くり返しのサンプラー

    ID3D11Device* m_device;
    ID3D11DeviceContext* m_context;

    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11PixelShader> m_pixelShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    ComPtr<ID3D11SamplerState> m_samplerState;
    ComPtr<ID3D11BlendState> m_blendState;
    ComPtr<ID3D11BlendState> m_blendAdd;   // 加算合成
    float m_whiteMix = 0.0f;               // 白を混ぜる量
    ComPtr<ID3D11PixelShader>  m_fxPS;     // エフェクト用のピクセルシェーダー（Fx_PS.hlsl）
    ComPtr<ID3D11Buffer>       m_fxCB;     // その数字（FxParams）
    ComPtr<ID3D11PixelShader>  m_dissolvePS;   // 溶けるシェーダー（Dissolve_PS.hlsl）
    ComPtr<ID3D11Buffer>       m_dissolveCB;   // その数字（DissolveParams）
    ComPtr<ID3D11SamplerState> m_samplerWrap;   // 端でくり返す読み方（ノイズを流す用）
    ComPtr<ID3D11RasterizerState> m_rasterState;
    ComPtr<ID3D11DepthStencilState> m_depthStencilState;
    ComPtr<ID3D11DepthStencilView> m_depthStencilView;
    ComPtr<ID3D11DepthStencilState> m_depthNoWriteState;
    ComPtr<ID3D11DepthStencilState> m_depthDisabledState;

    ComPtr<ID3D11Buffer> m_billboardVertexBuffer;
    ComPtr<ID3D11Buffer> m_quadVertexBuffer;   // DrawQuad 用（描くたびに四隅を書き換える）

    // ポストプロセス（ブラー）
    ComPtr<ID3D11Texture2D>          m_ppTex;
    ComPtr<ID3D11RenderTargetView>   m_ppRTV;
    ComPtr<ID3D11ShaderResourceView> m_ppSRV;
    ComPtr<ID3D11VertexShader>       m_blurVS;
    ComPtr<ID3D11PixelShader>        m_blurPS;
    ComPtr<ID3D11Buffer>             m_blurCB;
    ComPtr<ID3D11SamplerState>       m_ppSampler;
    ID3D11RenderTargetView* m_savedRTV = nullptr;
    ComPtr<ID3D11Buffer>       m_blurVB;
    ComPtr<ID3D11InputLayout>  m_blurLayout;
    D3D11_VIEWPORT m_savedVP = {};

    XMMATRIX m_viewMatrix;
    XMMATRIX m_projectionMatrix;

    // カメラ関係
    XMFLOAT3 m_cameraPos;
    float m_cameraPitchAngle; // カメラの仰角

    // スクリーン関係
    int m_screenWidth;
    int m_screenHeight;

};

