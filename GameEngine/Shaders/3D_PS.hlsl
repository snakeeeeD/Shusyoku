Texture2D txDiffuse : register(t0);
SamplerState samLinear : register(s0);

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
};

cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
    float4 Color;
    float4 UvRect;
    float4 Extra;   // x: white mix (0-1)
};

float4 main(PS_INPUT input) : SV_Target
{
    float4 c = txDiffuse.Sample(samLinear, input.Tex) * input.Color;
    c.rgb = lerp(c.rgb, float3(1, 1, 1), Extra.x);
    return c;
}