Texture2D tex : register(t0);
SamplerState samp : register(s0);
cbuffer BlurCB : register(b0)
{
    float2 texel;
    float radius;
    float pad;
};
struct PSIn
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};
float4 main(PSIn i) : SV_Target
{
    float4 sum = 0;
    float wsum = 0;
    [unroll]
    for (int y = -2; y <= 2; y++)
    [unroll]
        for (int x = -2; x <= 2; x++)
        {
            float2 off = float2(x, y) * texel * radius;
            sum += tex.Sample(samp, i.uv + off);
            wsum += 1.0;
        }
    return sum / wsum;
}