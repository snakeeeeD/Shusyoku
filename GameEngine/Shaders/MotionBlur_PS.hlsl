// 動きのブラー：キャラの絵を、元いた方向へ伸ばした板に描く。
//   1つの点ごとに、絵を「前の方へ少しずつずらして」TAPS 回読み、後ろの写しほど弱く（1－t）足し合わせる。
//   絵を何枚も描かずに、1回描くだけでぼける。本体の所は本体のまま（くっきり）
Texture2D texSprite : register(t0);
SamplerState samClamp : register(s0);

cbuffer ConstantBuffer : register(b0)   // いつもの板の数字
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 UvRect;
    float4 Extra;   // x: 白を混ぜる量
};

cbuffer MotionBlurParams : register(b1)
{
    float2 BlurUv;     // 後ろ（元いた方向）へ伸ばす向きと長さ（絵の uv で）
    float  Strength;   // ブラーの濃さ
    float  Pad0;
    float4 Rect;       // 本来の絵の uv の範囲（xy＝小さい方、zw＝大きい方）
};

struct PS_INPUT
{
    float4 Pos   : SV_POSITION;
    float2 Tex   : TEXCOORD;
    float4 Color : COLOR;
};

static const int TAPS = 20;   // 読む回数（長いブラーでも写しが段々に見えないように）

// 本来の絵の範囲の外は透明として読む（広げた板に、絵の端の色が伸びないように）
float4 ReadSprite(float2 uv)
{
    float inside = step(Rect.x, uv.x) * step(Rect.y, uv.y) * step(uv.x, Rect.z) * step(uv.y, Rect.w);   // 範囲の中なら1
    return texSprite.Sample(samClamp, uv) * inside;
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 base = ReadSprite(input.Tex);   // 本体
    // 後ろへ t（0→1）だけずらした写しが、ここに重なるか。後ろの写しほど薄く
    float3 col = 0; float a = 0; float wsum = 0;
    [unroll] for (int k = 1; k <= TAPS; k++)
    {
        float t = (float)k / TAPS;
        float w = sqrt(1.0 - t);   // 後ろの写しほど薄く。√ にして、後ろの方まで濃さを残す
        float4 s = ReadSprite(input.Tex - BlurUv * t);
        col += s.rgb * s.a * w; a += s.a * w; wsum += w;
    }
    float trailA = saturate(a / wsum * Strength);
    float3 trailC = (a > 0.0001) ? col / a : float3(0, 0, 0);
    // 本体の後ろにブラーを重ねる（本体が不透明な所は本体のまま）
    float outA = base.a + trailA * (1.0 - base.a);
    float3 outC = (outA > 0.0001) ? (base.rgb * base.a + trailC * trailA * (1.0 - base.a)) / outA : float3(0, 0, 0);
    float4 c = float4(outC, outA) * input.Color;
    c.rgb = lerp(c.rgb, float3(1, 1, 1), Extra.x);
    return c;
}
