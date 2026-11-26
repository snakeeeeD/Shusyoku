// 落ち影 その1：光のカメラから、影を落とす板を描く時のピクセルシェーダー
// 授業の PS_WriteDepth に「絵の透明な所を捨てる」を足したもの（板の四角ではなく、キャラや木の形の影にするため）
Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;   // 光のカメラから見た位置（z＝深さ 0～1）
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

float4 main(PS_INPUT input) : SV_Target
{
    float a = txDiffuse.Sample(samLinear, input.Tex).a * input.Color.a;
    clip(a - 0.5f);                                   // 透明な所は深さを書かない＝影にならない
    return float4(input.Pos.z, 0.0f, 0.0f, 1.0f);     // 光からの深さを R に書く
}
