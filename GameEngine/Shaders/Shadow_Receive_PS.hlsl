// 落ち影 その3：影を受ける地面のピクセルシェーダー
// 授業の PS_Shadow と同じく、光から見た深さを絵と比べる（授業は step で 0/1、ここは比べてから混ぜる）。違う所：
//   ・真っ黒ではなく、地面の色を暗くする（影の濃さ ShadowParams.x）
//   ・周り 3×3 を比べて平均し、影のふちをぼかす（ソフトシャドウ）
Texture2D txDiffuse : register(t0);     // 地面の絵
Texture2D shadowTex : register(t1);     // 光から見た深さ
SamplerState samLinear : register(s0);
SamplerComparisonState samShadow : register(s1);  // 比べてから混ぜる。絵の外は 1.0（影なし）

cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 UvRect;
    float4 Extra;
};

cbuffer ShadowBuffer : register(b1)
{
    matrix LightView;
    matrix LightProj;
    float4 ShadowParams;
};

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
    float4 LightPos : POSITION1;
};

// 光が届いている割合（1＝日なた、0＝影）
float LitAmount(float4 lightPos)
{
    // 光のカメラの画面の位置 → 深さの絵の uv（授業の PS_Shadow と同じ）
    float2 uv = lightPos.xy / lightPos.w;
    uv = uv * 0.5f + 0.5f;
    uv.y = 1.0f - uv.y;
    // この点の光からの深さ。少し手前で比べる（ずれ止め：同じ深さどうしで影がちらつかないように）
    float depth = lightPos.z / lightPos.w - ShadowParams.y;

    float lit = 0.0f;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++)
        {
            // 絵の深さより手前（＝何もさえぎっていない）なら 1。隣り合う4点の結果を GPU がなめらかに混ぜる
            lit += shadowTex.SampleCmpLevelZero(samShadow, uv + float2(x, y) * ShadowParams.z, depth);
        }
    return lit / 9.0f;
}

float4 main(PS_INPUT input) : SV_Target
{
    float4 c = txDiffuse.Sample(samLinear, input.Tex) * input.Color;
    c.rgb = lerp(c.rgb, float3(1, 1, 1), Extra.x);   // 3D_PS と同じ「白を混ぜる」
    c.rgb *= lerp(1.0f - ShadowParams.x, 1.0f, LitAmount(input.LightPos));
    return c;
}
