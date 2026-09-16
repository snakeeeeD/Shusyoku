cbuffer ConstantBuffer : register(b0)
{
    matrix World;
    matrix Projection;
    float4 Color;
    float4 UVRect;
    float4 Fade;
};
struct VS_INPUT
{
    float3 Pos : POSITION;
    float2 Tex : TEXCOORD;
};
struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 Tex : TEXCOORD;
    float4 Color : COLOR;
    float WY : TEXCOORD1;
};

PS_INPUT main(VS_INPUT input)
{
    PS_INPUT output;
    float4 wp = mul(float4(input.Pos, 1.0f), World); // ÉèÅ[ÉãÉh=âÊñ pxç¿ïW
    output.Pos = mul(wp, Projection);
    output.Tex = input.Tex * UVRect.zw + UVRect.xy;
    output.Color = Color;
    output.WY = wp.y; // âÊñ Y
    return output;
}